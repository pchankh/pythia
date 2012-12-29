// Copyright 2012 Eugen Sawin <esawin@me73.com>
#ifndef SRC_NET_SERVER_APPLICATION_H_
#define SRC_NET_SERVER_APPLICATION_H_

#include "./server.h"
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/ThreadPool.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <string>
#include <vector>
#include <ostream>
#include <fstream>
#include "./request-handler-factory.h"

using std::string;
using std::vector;
using Poco::Net::ServerSocket;
using Poco::Net::HTTPServer;
using Poco::Net::HTTPServerParams;
using Poco::ThreadPool;
using Poco::Util::ServerApplication;
using Poco::Util::Application;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::OptionCallback;
using pyt::nlp::Tagger;

namespace pyt {
namespace net {

DEFINE_string(api, "api.txt", "Google API key + CX file.");

Server::Server(const string& name, const string& version,
               const string& doc_path, const uint16_t port,
               const uint16_t threads, const uint16_t queue_size)
    : name_(name),
      version_(version),
      doc_path_(doc_path),
      port_(port),
      num_threads_(threads),
      queue_size_(queue_size),
      tagger_(Tagger::kPos) {
  std::fstream file(FLAGS_api);
  LOG_IF(FATAL, !file.good()) << "Google API file " << FLAGS_api
                               << " not found.";
  std::getline(file, api_key_);
  LOG_IF(FATAL, file.eof()) << "Provided Google API file " << FLAGS_api
                            << " has invalid format. Should be <key>\n<cx>.";
  std::getline(file, api_cx_);
  search_host_ = "https://www.googleapis.com";
  search_base_ = "/customsearch/v1?";
  search_base_ += "key=" + api_key_;
  search_base_ += "&cx=" + api_cx_;
  search_base_ += "&q=";
}

void Server::Run() {
  vector<string> args = {"pythia"};
  run(args);
}

const string& Server::DocumentPath() const {
  return doc_path_;
}

const string& Server::ApiKey() const {
  return api_key_;
}

const string& Server::ApiCx() const {
  return api_cx_;
}

const string& Server::SearchHost() const {
  return search_host_;
}

const string& Server::SearchBase() const {
  return search_base_;
}

const Tagger& Server::Tagger() const {
  return tagger_;
}

void Server::initialize(Application& self) {  // NOLINT
  ServerApplication::initialize(self);
  Poco::Net::SSLManager::instance().initializeClient(
      0, 0, new Poco::Net::Context(Poco::Net::Context::CLIENT_USE, "",
                                   Poco::Net::Context::VERIFY_NONE, 9, false,
                                   "ALL:!ADH:!LOW:!EXP!MD5:@STRENGTH"));
}

void Server::uninitialize() {
  ServerApplication::uninitialize();
}

int Server::main(const vector<string>& args) {
  ServerSocket socket(port_);
  HTTPServerParams* params = new HTTPServerParams();
  params->setServerName(name_);
  params->setSoftwareVersion(version_);
  params->setMaxQueued(queue_size_);
  params->setMaxThreads(num_threads_);
  HTTPServer server(new RequestHandlerFactory(), socket, params);
  server.start();
  waitForTerminationRequest();
  server.stop();
  return Application::EXIT_OK;
}

}  // namespace net
}  // namespace pyt
#endif  // SRC_NET_SERVER_APPLICATION_H_
