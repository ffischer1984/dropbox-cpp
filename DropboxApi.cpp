/*
* Copyright (c) 2013 Rahul Iyer
* All rights reserved.
* 
* Redistribution and use in source and binary forms are permitted provided that
* the above copyright notice and this paragraph are duplicated in all such forms
* and that any documentation, advertising materials, and other materials related
* to such distribution and use acknowledge that the software was developed by 
* Rahul Iyer.  The name of Rahul Iyer may not be used to endorse or promote 
* products derived from this software without specific prior written permission.
* THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF 
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*/

#include "DropboxApi.h"
#include "util/HttpRequest.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <sstream>
#include <cassert>

using namespace dropbox;
using namespace http;
using namespace std;

using namespace boost::property_tree;
using namespace boost::property_tree::json_parser;

// Dropbox API v2 base URLs
// NOTE: content endpoint uses content.dropboxapi.com (NOT content.dropbox.com)
static const char* kApiBase     = "https://api.dropbox.com/2";
static const char* kContentBase = "https://content.dropboxapi.com/2";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DropboxApi::DropboxApi(const string& clientId, const string& clientSecret) {
  httpFactory_ = HttpRequestFactory::createFactory();
  lock_guard<mutex> g(stateLock_);
  auth_.reset(new DropboxAuth(clientId, clientSecret));
}

DropboxApi::DropboxApi(const string& clientId,
    const string& clientSecret,
    const string& accessToken) {
  httpFactory_ = HttpRequestFactory::createFactory();
  lock_guard<mutex> g(stateLock_);
  auth_.reset(new DropboxAuth(clientId, clientSecret));
  auth_->setAccessToken(accessToken);
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

void DropboxApi::authenticate(
    function<string(const string& authUrl)> cb) {
  lock_guard<mutex> g(stateLock_);
  string url  = auth_->getAuthorizationUrl("", "", /*requestOffline=*/true);
  string code = cb(url);
  auth_->exchangeCode(code);
}

void DropboxApi::setAccessToken(const string& token) {
  lock_guard<mutex> g(stateLock_);
  auth_->setAccessToken(token);
}

string DropboxApi::getAccessToken() const {
  lock_guard<mutex> g(stateLock_);
  return auth_->getAccessToken();
}

void DropboxApi::setRefreshToken(const string& token) {
  lock_guard<mutex> g(stateLock_);
  auth_->setRefreshToken(token);
}

string DropboxApi::getRefreshToken() const {
  lock_guard<mutex> g(stateLock_);
  return auth_->getRefreshToken();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::execute(shared_ptr<HttpRequest> r) {
  int ret;
  {
    lock_guard<mutex> g(stateLock_);
    auth_->addBearerHeader(r.get());
  }
  if ((ret = r->execute())) {
    stringstream ss;
    ss << "Curl error (code = " << ret << ")";
    throw DropboxException(CURL_ERROR, ss.str());
  }
  return static_cast<DropboxErrorCode>(r->getResponseCode());
}

// Parse a v2 response whose metadata lives under a named sub-key,
// e.g. delete_v2 / copy_v2 / move_v2 / create_folder_v2 return
// { "metadata": { ... } }.
void DropboxApi::parseWrappedMetadata(const string& json,
    const string& key, DropboxMetadata& m) {
  stringstream ss;
  ss << json;
  ptree pt;
  read_json(ss, pt);
  auto child = pt.get_child(key);
  DropboxMetadata::readFromJson(child, m);
}

// ---------------------------------------------------------------------------
// Account
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::getAccountInfo(DropboxAccountInfo& info) {
  // Step 1: get_current_account
  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/users/get_current_account"));
  r->setMethod(HttpPostRequest);
  // v2 requires an empty JSON body ("null") for no-arg endpoints
  r->setJsonBody("null");

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  info.readJson(response);

  // Step 2: get_space_usage
  shared_ptr<HttpRequest> r2(httpFactory_->createHttpRequest(
    string(kApiBase) + "/users/get_space_usage"));
  r2->setMethod(HttpPostRequest);
  r2->setJsonBody("null");

  code = execute(r2);
  if (code != SUCCESS) { return code; }

  string usageResponse((char*)r2->getResponse(), r2->getResponseSize());
  info.readSpaceUsageJson(usageResponse);

  return SUCCESS;
}

// ---------------------------------------------------------------------------
// File download
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::getFile(DropboxGetFileRequest& req,
    DropboxGetFileResponse& res) {
  // v2: POST https://content.dropbox.com/2/files/download
  // Path and rev are passed in the Dropbox-API-Arg header as JSON.
  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kContentBase) + "/files/download"));
  r->setMethod(HttpPostRequest);

  // Build Dropbox-API-Arg header
  stringstream arg;
  arg << "{\"path\":\"" << req.getPath() << "\"";
  if (!req.getRev().empty()) {
    arg << ",\"rev\":\"" << req.getRev() << "\"";
  }
  arg << "}";
  r->addHeader("Dropbox-API-Arg", arg.str());
  // Explicitly set empty body (no JSON body for content endpoints)
  r->addHeader("Content-Type", "");

  if (req.hasRange()) {
    std::stringstream rs;
    rs << "bytes=" << req.getOffset() << "-"
       << (req.getOffset() + req.getLength() - 1);
    r->addHeader("Range", rs.str());
  }

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS && code != PARTIAL_CONTENT) { return code; }

  res.setData(r->getResponse(), r->getResponseSize());

  // v2 returns metadata in Dropbox-API-Result response header.
  // Headers are stored lowercase (HTTP/2 normalisation applied in headerFunction).
  auto respHeaders = r->getResponseHeaders();
  auto it = respHeaders.find("dropbox-api-result");
  if (it != respHeaders.end()) {
    string meta = it->second;
    // Trim leading whitespace that curl includes after the colon
    size_t start = meta.find_first_not_of(" \t");
    if (start != string::npos) { meta = meta.substr(start); }
    res.setMetadata(meta);
  }

  return code;
}

// ---------------------------------------------------------------------------
// Metadata / folder listing
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::getFileMetadata(DropboxMetadataRequest& req,
    DropboxMetadataResponse& res) {
  string endpoint;
  stringstream body;

  if (req.includeChildren()) {
    // list_folder
    endpoint = string(kApiBase) + "/files/list_folder";
    body << "{"
         << "\"path\":\""           << req.path()        << "\""
         << ",\"include_deleted\":" << (req.includeDeleted() ? "true" : "false")
         << "}";
  } else {
    // get_metadata
    endpoint = string(kApiBase) + "/files/get_metadata";
    body << "{"
         << "\"path\":\"" << req.path() << "\""
         << "}";
  }

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(endpoint));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  res.readJson(response);
  return code;
}

// ---------------------------------------------------------------------------
// Revisions
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::getRevisions(const string& path,
    size_t numRevisions, DropboxRevisions& revs) {
  stringstream body;
  body << "{\"path\":\"" << path << "\"";
  if (numRevisions) {
    body << ",\"limit\":" << numRevisions;
  }
  body << "}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/files/list_revisions"));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  revs.readFromJson(response);
  return code;
}

// ---------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::restoreFile(const string& path,
    const string& rev, DropboxMetadata& m) {
  stringstream body;
  body << "{\"path\":\"" << path << "\",\"rev\":\"" << rev << "\"}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/files/restore"));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  stringstream ss;
  ss << response;
  ptree pt;
  read_json(ss, pt);
  DropboxMetadata::readFromJson(pt, m);
  return code;
}

// ---------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::deleteFile(const string& path, DropboxMetadata& m) {
  stringstream body;
  body << "{\"path\":\"" << path << "\"}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/files/delete_v2"));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  // delete_v2 wraps metadata under "metadata" key
  parseWrappedMetadata(response, "metadata", m);
  return code;
}

// ---------------------------------------------------------------------------
// Copy / Move (shared helper)
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::copyOrMove(const string& from,
    const string& to, const string& endpoint, DropboxMetadata& m) {
  stringstream body;
  body << "{\"from_path\":\"" << from << "\",\"to_path\":\"" << to << "\"}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/files/" + endpoint));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  // copy_v2 / move_v2 wrap metadata under "metadata" key
  parseWrappedMetadata(response, "metadata", m);
  return code;
}

DropboxErrorCode DropboxApi::copyFile(const string& from,
    const string& to, DropboxMetadata& m) {
  return copyOrMove(from, to, "copy_v2", m);
}

DropboxErrorCode DropboxApi::moveFile(const string& from,
    const string& to, DropboxMetadata& m) {
  return copyOrMove(from, to, "move_v2", m);
}

// ---------------------------------------------------------------------------
// Create folder
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::createFolder(const string& path, DropboxMetadata& m) {
  stringstream body;
  body << "{\"path\":\"" << path << "\",\"autorename\":false}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/files/create_folder_v2"));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  // create_folder_v2 wraps metadata under "metadata" key.
  // Note: the response FolderMetadata does not include ".tag", so we set
  // isDir_/tag_ explicitly after parsing (the endpoint always returns a folder).
  parseWrappedMetadata(response, "metadata", m);
  m.tag_     = "folder";
  m.isDir_   = true;
  m.isDeleted_ = false;
  return code;
}

// ---------------------------------------------------------------------------
// Upload (small file)
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::uploadFile(const DropboxUploadFileRequest& req,
    DropboxMetadata& m) {
  // Determine upload mode and autorename setting
  string mode;
  bool autorename = false;
  if (!req.getParentRev().empty()) {
    // Update a specific revision
    mode = string("{\".\\.tag\":\"update\",\"update\":\"") +
           req.getParentRev() + "\"}";
  } else if (req.shouldOverwrite()) {
    mode = "\"overwrite\"";
  } else {
    // "add" mode: if the file already exists Dropbox will auto-rename it
    mode = "\"add\"";
    autorename = true;
  }

  // Build Dropbox-API-Arg header
  stringstream arg;
  arg << "{\"path\":\"" << req.getPath() << "\""
      << ",\"mode\":"  << mode
      << ",\"autorename\":" << (autorename ? "true" : "false")
      << "}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kContentBase) + "/files/upload"));
  r->setMethod(HttpPostRequest);
  r->addHeader("Dropbox-API-Arg",  arg.str());
  r->addHeader("Content-Type",     "application/octet-stream");

  assert(req.getUploadData());
  r->setRequestData(req.getUploadData(), req.getUploadDataSize());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  stringstream ss;
  ss << response;
  ptree pt;
  read_json(ss, pt);
  DropboxMetadata::readFromJson(pt, m);
  return code;
}

// ---------------------------------------------------------------------------
// Upload (large file – upload session)
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::uploadLargeFile(
    const DropboxUploadLargeFileRequest& req,
    DropboxMetadata& m) {
  string sessionId;
  size_t offset = req.getOffset();
  size_t size   = 0;

  unique_ptr<uint8_t, void(*)(void*)> data(
    static_cast<uint8_t*>(malloc(req.getChunkSize())), free);
  if (!data) { throw std::bad_alloc(); }

  // ------------------------------------------------------------------
  // Phase 1: upload_session/start  (first chunk)
  // ------------------------------------------------------------------
  size = req.getData(data.get(), offset, req.getChunkSize());
  if (size > 0) {
    shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
      string(kContentBase) + "/files/upload_session/start"));
    r->setMethod(HttpPostRequest);
    r->addHeader("Dropbox-API-Arg",
      string("{\"close\":") + (size < req.getChunkSize() ? "true" : "false") + "}");
    r->addHeader("Content-Type", "application/octet-stream");
    r->setRequestData(data.get(), size);

    DropboxErrorCode code = execute(r);
    if (code != SUCCESS) { return code; }

    string response((char*)r->getResponse(), r->getResponseSize());
    auto cursor = DropboxUploadSessionCursor::readFromJson(response);
    sessionId = cursor.getSessionId();
    offset   += size;
  }

  // ------------------------------------------------------------------
  // Phase 2: upload_session/append_v2  (subsequent chunks)
  // ------------------------------------------------------------------
  while ((size = req.getData(data.get(), offset, req.getChunkSize())) > 0) {
    if (static_cast<ssize_t>(size) < 0) {
      throw DropboxException(IO_ERROR, "Error reading file data");
    }

    bool isLast = (size < req.getChunkSize());

    stringstream arg;
    arg << "{"
        << "\"cursor\":{\"session_id\":\"" << sessionId
        <<              "\",\"offset\":"   << offset << "}"
        << ",\"close\":"                   << (isLast ? "true" : "false")
        << "}";

    shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
      string(kContentBase) + "/files/upload_session/append_v2"));
    r->setMethod(HttpPostRequest);
    r->addHeader("Dropbox-API-Arg",  arg.str());
    r->addHeader("Content-Type",     "application/octet-stream");
    r->setRequestData(data.get(), size);

    DropboxErrorCode code = execute(r);
    if (code != SUCCESS) { return code; }

    offset += size;
  }

  // ------------------------------------------------------------------
  // Phase 3: upload_session/finish  (commit)
  // ------------------------------------------------------------------
  string mode;
  if (!req.getParentRev().empty()) {
    mode = string("{\".\\.tag\":\"update\",\"update\":\"") +
           req.getParentRev() + "\"}";
  } else if (req.shouldOverwrite()) {
    mode = "\"overwrite\"";
  } else {
    mode = "\"add\"";
  }

  stringstream finishArg;
  finishArg << "{"
            << "\"cursor\":{\"session_id\":\"" << sessionId
            <<              "\",\"offset\":"   << offset << "}"
            << ",\"commit\":{"
            <<   "\"path\":\""  << req.getPath() << "\""
            <<   ",\"mode\":"   << mode
            <<   ",\"autorename\":false"
            << "}"
            << "}";

  shared_ptr<HttpRequest> rf(httpFactory_->createHttpRequest(
    string(kContentBase) + "/files/upload_session/finish"));
  rf->setMethod(HttpPostRequest);
  rf->addHeader("Dropbox-API-Arg",  finishArg.str());
  rf->addHeader("Content-Type",     "application/octet-stream");
  // finish endpoint requires an empty body
  uint8_t empty = 0;
  rf->setRequestData(&empty, 0);

  DropboxErrorCode code = execute(rf);
  if (code != SUCCESS) { return code; }

  string response((char*)rf->getResponse(), rf->getResponseSize());
  stringstream ss;
  ss << response;
  ptree pt;
  read_json(ss, pt);
  DropboxMetadata::readFromJson(pt, m);
  return code;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

DropboxErrorCode DropboxApi::search(const DropboxSearchRequest& req,
    DropboxSearchResult& res) {
  // Build options sub-object
  stringstream body;
  body << "{"
       << "\"query\":\"" << req.getSearchQuery() << "\""
       << ",\"options\":{"
       <<   "\"path\":\""        << req.getSearchPath()  << "\""
       <<   ",\"max_results\":"  << req.getResultLimit()
       <<   ",\"include_highlights\":false"
       << "}"
       << "}";

  shared_ptr<HttpRequest> r(httpFactory_->createHttpRequest(
    string(kApiBase) + "/files/search_v2"));
  r->setMethod(HttpPostRequest);
  r->setJsonBody(body.str());

  DropboxErrorCode code = execute(r);
  if (code != SUCCESS) { return code; }

  string response((char*)r->getResponse(), r->getResponseSize());
  res = DropboxSearchResult::readFromJson(response);

  return code;
}
