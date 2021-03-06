// Copyright 2012 Eugen Sawin <esawin@me73.com>
#include "./query-analyser.h"
#include "./tagger.h"

using std::string;
using std::vector;

namespace pyt {
namespace nlp {

QueryAnalyser::QueryAnalyser(const Tagger& tagger)
    : tagger_(tagger) {}

vector<string> QueryAnalyser::TargetKeywords(const string& query) const {
  auto IsNN = [](const int label) {
    return label == Tagger::kPosNN || label == Tagger::kPosNNP ||
           label == Tagger::kPosNNS || label == Tagger::kPosNNPS;
  };

  vector<string> keywords;
  vector<Tagger::Tag> tags = tagger_.Tags(query);
  for (auto beg = tags.cbegin(), end = tags.cend(), it = beg; it != end; ++it) {
    const int label = it->label;
    if (IsNN(label)) {
      if (keywords.empty() ||
          (it - beg > 1 && (it - 1)->label == Tagger::kPosCC &&
           IsNN((it - 2)->label))) {
        // Simple NN or NN conjunction.
        keywords.push_back(query.substr(it->offset.begin, it->offset.size));
      } else if (it != beg && IsNN((it - 1)->label)) {
        // Multi-word NN.
        keywords.back() += " " +
            query.substr(it->offset.begin, it->offset.size);
      } else if (keywords.size()) {
        // Found all relevant target keywords.
        break;
      }
    } else if (label == Tagger::kPosPOS && keywords.size()) {
      // Possesive ending, target NN is following.
      keywords.pop_back();
    }
  }
  return keywords;
}

vector<string> QueryAnalyser::Keywords(const string& query,
    const vector<string>& target_keywords) const {
  auto IsNN = [](const int label) {
    return label == Tagger::kPosNN || label == Tagger::kPosNNP ||
           label == Tagger::kPosNNS || label == Tagger::kPosNNPS;
  };

  auto IsJJ = [](const int label) {
    return label == Tagger::kPosJJ || label == Tagger::kPosJJR ||
           label == Tagger::kPosJJS;
  };

  vector<string> keywords;
  const size_t num_target_keywords = target_keywords.size();

  // We need a first upper case letter and punctuation for the POS-tagger.
  string prop_query = query + ".";
  prop_query[0] = std::toupper(prop_query[0]); 
  vector<Tagger::Tag> tags = tagger_.Tags(prop_query);
  for (auto beg = tags.cbegin(), end = tags.cend(), it = beg; it != end; ++it) {
    const int label = it->label;
    if (IsNN(label) || IsJJ(label)) {
      keywords.push_back(query.substr(it->offset.begin, it->offset.size));
      for (size_t pos = 0; pos < num_target_keywords; ++pos) {
        if (target_keywords[pos].find(keywords.back()) != string::npos) {
          // Ignore target keywords.
          keywords.pop_back();
          break;
        }
      }
    }
  }
  return keywords;
}

}  // namespace nlp
}  // namespace pyt
