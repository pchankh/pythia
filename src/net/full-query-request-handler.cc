// Copyright 2012, 2013 Eugen Sawin <esawin@me73.com>
#include "./full-query-request-handler.h"
#include <Poco/Exception.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/DefaultHandler.h>
#include <Poco/JSON/Query.h>
#include <glog/logging.h>
#include <flow/clock.h>
#include <flow/string.h>
#include <flow/stringify.h>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <regex>
#include "./server.h"
#include "./query-parser.h"
#include "./http-request.h"
#include "../nlp/entity-index.h"
#include "../nlp/named-entity-extractor.h"
#include "../nlp/edit-distance.h"
#include "../nlp/grammar-tools.h"

using std::string;
using std::vector;
using std::pair;
using std::tuple;
using std::get;
using pyt::nlp::Tagger;
using pyt::nlp::NamedEntityExtractor;
using pyt::nlp::EntityIndex;
using pyt::nlp::Entity;
using pyt::nlp::EditDistance;
using pyt::nlp::PrefixEditDistance;
using pyt::nlp::OntologyIndex;
using pyt::nlp::SingularForms;
using flow::time::ThreadClock;
using flow::time::Clock;
using flow::time::ClockDiff;
using flow::io::JsonArray;

namespace pyt {
namespace net {

string StripHtml(const string& content) {
  std::stringstream ss;
  const size_t end = content.find("</body");
  size_t pos = content.find("<body");
  while (pos != string::npos && pos < end) {
    const size_t beg = content.find(">", pos) + 1;
    pos = content.find("<", beg);
    if (beg != string::npos) {
      ss << content.substr(beg, pos - beg);
    }
  }
  return ss.str();
}

FullQueryRequestHandler::FullQueryRequestHandler(const Poco::URI& uri)
    : server_(static_cast<Server&>(Poco::Util::Application::instance())),
      uri_(uri),
      query_analyser_(server_.Tagger()) {}

// TODO(esawin): This need heavy refactoring.
void FullQueryRequestHandler::Handle(Request* request, Response* response) {
  static const int64_t timeout = 3 * 1e6;  // Microseconds.

  Clock request_start_time;
  Clock start_time;
  Clock end_time;
  // Prepare response stream.
  response->setChunkedTransferEncoding(true);
  response->setContentType("text/plain");
  std::ostream& response_stream = response->send();

  const Query query(uri_.getQuery());
  const string& query_text = query.Text("qf");
  const string& query_uri = query.Uri("qf");
  LOG(INFO) << "Full query request: " << query_text << ".";

  // Get the target keywords.
  vector<string> target_keywords = query_analyser_.TargetKeywords(query_text);
  vector<string> keywords = query_analyser_.Keywords(query_text,
      target_keywords);
  LOG(INFO) << "Target keywords: " << target_keywords;

  end_time = Clock();

  response_stream << "{\"query_analysis\":{"
      << "\"duration\":" << (end_time - start_time).Value() << ","
      << "\"query\":" << JsonArray(query.Words("qf")) << ","
      << "\"keywords\":" << JsonArray(keywords) << ","
      << "\"target_keywords\":" << JsonArray(target_keywords) << "}";

  start_time = end_time;

  auto& web_cache = server_.WebCache();
  // Get Google search results.
  const string search_url = server_.SearchHost() + server_.SearchBase() +
      query_uri;
  auto response_it = web_cache.find(search_url);
  if (response_it == web_cache.end()) {
    response_it = web_cache.insert({search_url,
        HttpsGetRequest(search_url, timeout)}).first;
  }
  Poco::JSON::Parser json_parser;
  Poco::JSON::DefaultHandler json_handler;
  json_parser.setHandler(&json_handler);
  json_parser.parse(response_it->second);
  Poco::Dynamic::Var json_data = json_handler.result();
  Poco::JSON::Query json_query(json_data);

  // Get page contents and extract named entities.
  const OntologyIndex& ontology = server_.OntologyIndex();
  EntityIndex index;
  auto items = json_query.findArray("items");
  
  end_time = Clock();

  response_stream << ",\"document_retrieval\":{"
      << "\"duration\":" << (end_time - start_time).Value()
      << ",\"documents\":";
  items->stringify(response_stream, 0);
  response_stream << "}";

  start_time = end_time;

  NamedEntityExtractor extractor;
  const size_t num_items = items->size();
  vector<vector<std::pair<string, Entity::Type>>> extracted_content(num_items);
  vector<vector<std::pair<string, Entity::Type>>> extracted_snippets(num_items);
  #pragma omp parallel for
  for (size_t i = 0; i < num_items; ++i) {
    const string& url = items->getObject(i)->getValue<string>("link");
    auto content_it = web_cache.find(url);
    auto snippet_it = web_cache.find("snippet/" + url);
    if (content_it == web_cache.end()) {
      #pragma omp critical
      content_it = web_cache.insert({url,
          StripHtml(HttpGetRequest(url, timeout))}).first;
      const string& snippet = items->getObject(i)->getValue<string>("snippet");
      #pragma omp critical
      snippet_it = web_cache.insert({"snippet/" + url, snippet}).first;
    }
    #pragma omp critical
    {
      extractor.Extract(content_it->second, &extracted_content[i]);
      extractor.Extract(snippet_it->second, &extracted_snippets[i]);
    }
  }

  auto IsBadName = [](const string& word) {
    for (const char c: word) {
      if (!std::isalpha(c) && c != ' ' && c != '-' && c != '\'') {
        return true;
      }
    }
    return word.size() < 2 || word.size() > 40;
  };

  end_time = Clock();

  response_stream << ",\"entity_extraction\":{"
      << "\"duration\":" << (end_time - start_time).Value() << "}";

  start_time = end_time;

  // const float log_sum_keywords = std::log2(server_.SumKeywordFreqs());
  // "keyword:coarse-type" ->
  // (content-freq, snippet-freq, score, total-frequency).
  std::unordered_map<string, tuple<int, int, int, int>> content_entities;
  static const float max_log_lhs_freq = 23.0f;
  for (size_t i = 0; i < num_items; ++i) {
    for (auto e: extracted_content[i]) {
      std::transform(e.first.begin(), e.first.end(), e.first.begin(),
          ::tolower);
      flow::string::Replace("\"", "", &e.first);
      if (IsBadName(e.first)) {
        continue;
      }
      string space_free = e.first;
      flow::string::Replace(" ", "", &space_free);
      const int ontology_id = ontology.LhsNameId(space_free);
      const string entity_key = e.first + ":" + Entity::TypeName(e.second);
      float idf = 0.0f;
      if (ontology_id == OntologyIndex::kInvalidId ||
          ontology.LhsFrequency(ontology_id) == 0) {
        // Ignore unkown entities.
        // continue;
      } else {
        idf = max_log_lhs_freq - std::log2(ontology.LhsFrequency(ontology_id));
        get<3>(content_entities[entity_key]) = ontology.LhsFrequency(ontology_id);
      }
      get<0>(content_entities[entity_key]) += 1;
      // float log_keyword_freq = log_sum_keywords - 1.0;
      // auto it = server_.KeywordFreqs().find(space_free);
      // if (it != server_.KeywordFreqs().end()) {
        // log_keyword_freq = log2(it->second);
      // }
      // const float ikf = log_sum_keywords - log_keyword_freq;
      index.Add(e.first, e.second, i, (num_items - i) * idf);
    }
    for (auto e: extracted_snippets[i]) {
      std::transform(e.first.begin(), e.first.end(), e.first.begin(),
          ::tolower);
      flow::string::Replace("\"", "", &e.first);
      if (IsBadName(e.first)) {
        continue;
      }
      string space_free = e.first;
      flow::string::Replace(" ", "", &space_free);
      const int ontology_id = ontology.LhsNameId(space_free);
      const string entity_key = e.first + ":" + Entity::TypeName(e.second);
      float idf = 0.0f;
      if (ontology_id == OntologyIndex::kInvalidId ||
          ontology.LhsFrequency(ontology_id) == 0) {
        // Ignore unkown entities.
        // continue;
      } else {
        idf = max_log_lhs_freq - std::log2(ontology.LhsFrequency(ontology_id));
        get<3>(content_entities[entity_key]) = ontology.LhsFrequency(ontology_id);
      }
      get<1>(content_entities[entity_key]) += 1;
      // float log_keyword_freq = log_sum_keywords - 1.0;
      // auto it = server_.KeywordFreqs().find(space_free);
      // if (it != server_.KeywordFreqs().end()) {
        // log_keyword_freq = log2(it->second);
      // }
      // const float ikf = log_sum_keywords - log_keyword_freq;
      index.Add(e.first, e.second, i + num_items, 9.0f * (num_items - i) * idf);
    }
  }
  const vector<string>& query_words = query.Words("qf");

  auto IsSimilarToQuery = [&query_words](const string& word) {
    for (const string& w: query_words) {
      const int ped = PrefixEditDistance(w, word);
      if (ped <= std::abs((w.size() - 1) / 3)) {
        return true;
      }
    }
    return false;
  };

  response_stream << ",\"top_entities\":[";
  // Find the top candidates.
  const size_t num_top = 20;
  size_t current_num = 0;
  std::unordered_map<string, int> entities;
  string top_candidates;
  while (current_num < num_top && index.QueueSize()) {
    Entity entity = index.PopTop();
    if (entities.count(entity.name) || IsSimilarToQuery(entity.name) ||
        IsBadName(entity.name)) {
      // DLOG(INFO) << "Filtered entity: " << entity.name;
      continue;
    }
    top_candidates += (top_candidates.size() ? ", ": "") + entity.name;
    if (current_num) {
      response_stream << ",";
    }
    ++current_num;
    entities.insert({entity.name, entity.score});
    const string entity_key = entity.name + ":" + Entity::TypeName(entity.type);
    const auto it = content_entities.find(entity_key);
    LOG_IF(FATAL, it == content_entities.end()) << "Unkown entity top scored";
    response_stream << "[\"" << entity.name
                    << "\"," << get<0>(it->second)
                    << "," << get<1>(it->second)
                    << "," << entity.score
                    << "," << get<3>(it->second) << "]";
    get<2>(it->second) = entity.score;
  }
  response_stream << "],\"entities\":" << JsonArray(content_entities);

  end_time = Clock();

  response_stream << ",\"entity_ranking\":{"
      << "\"duration\":" << (end_time - start_time).Value() << "}";

  start_time = end_time;

  // Find target types.
  vector<string> target_types;
  for (const string& w: target_keywords) {
    // Add types occuring on the right-hand side of an is-a relation, which are
    // singular forms of the target keywords.
    const vector<string> singulars = SingularForms(w);
    for (const string& s: singulars) {
      // Check if a singular form of a target keyword is an ontology class.
      if (ontology.RhsNameId(s) != OntologyIndex::kInvalidId) {
        target_types.push_back(s + "*");
      }
    }
  }
  // TODO(esawin): Refactor this out of here.
  // Here comes the target type guessing based on most frequent entity types.
  const int is_a_relation_id = ontology.RelationId("is-a");
  DLOG_IF(FATAL, is_a_relation_id == OntologyIndex::kInvalidId)
      << "is-a relation is not indexed.";
  vector<std::pair<int, float> > rhs_freqs;
  for (const auto& e: entities) {
    const auto& relations = ontology.RelationsByLhs(e.first);
    for (const auto& r: relations) {
      if (r.first == is_a_relation_id) {
        rhs_freqs.push_back({r.second, e.second});
      }
    }
  }
  std::sort(rhs_freqs.begin(), rhs_freqs.end());
  vector<std::pair<float, int> > rhs_sorted;
  const float log_num_triples = 23;  // std::log2(ontology.NumTriples());
  for (const auto& rhs: rhs_freqs) {
    const float idf = log_num_triples -
        std::log2(ontology.RhsFrequency(rhs.first));
    const float score = idf * rhs.second;
    if (rhs_sorted.size() && rhs_sorted.back().second == rhs.first) {
        rhs_sorted.back().first += score;
    } else {
      rhs_sorted.push_back({score, rhs.first});
    }
  }
  const size_t k = std::min(4ul, rhs_sorted.size());
  std::partial_sort(rhs_sorted.begin(), rhs_sorted.begin() + k,
                    rhs_sorted.end(), std::greater<std::pair<float, int> >());
  rhs_sorted.resize(k);
  for (const auto& rhs: rhs_sorted) {
    target_types.push_back(ontology.Name(rhs.second));
    // LOG(INFO) << rhs.second << ": " << rhs.first;
  }
  end_time = Clock();

  response_stream << ",\"semantic_query_construction\":{"
      << "\"duration\":" << (end_time - start_time).Value() << "}";

  start_time = end_time;

  LOG(INFO) << "Top candidates: " << top_candidates;
  response_stream << ",\"target_types\":" << JsonArray(target_types);

  // Construct the Broccoli query.
  response_stream << ",\"broccoli_query\":\"$1 :r:is-a " << target_types[0]
                  << "; $1 :r:occurs-with "
                  << flow::io::Str(keywords, " ", "", "", "", "", "", "")
                  << "\"";

  end_time = Clock();
  response_stream << ",\"duration\":"
      << (end_time - request_start_time).Value() << "}";
}

}  // namespace net
}  // namespace pyt
