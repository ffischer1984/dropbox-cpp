/*
 * Dropbox API v2 – OAuth 2.0 authentication helper implementation.
 */

#include "DropboxAuth.h"
#include "HttpRequest.h"
#include "HttpRequestFactory.h"
#include "../DropboxException.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <sstream>

using namespace dropbox;
using namespace http;
using namespace std;
using namespace boost::property_tree;
using namespace boost::property_tree::json_parser;

static const char* kTokenUrl     = "https://api.dropbox.com/oauth2/token";
static const char* kAuthorizeUrl = "https://www.dropbox.com/oauth2/authorize";

DropboxAuth::DropboxAuth(const string& clientId, const string& clientSecret)
  : clientId_(clientId),
    clientSecret_(clientSecret),
    httpFactory_(HttpRequestFactory::createFactory()) {
}

string DropboxAuth::getAuthorizationUrl(
    const string& redirectUri,
    const string& state,
    bool requestOffline) const {
  stringstream ss;
  ss << kAuthorizeUrl
     << "?client_id="     << clientId_
     << "&response_type=code";
  if (!redirectUri.empty()) { ss << "&redirect_uri=" << redirectUri; }
  if (!state.empty())       { ss << "&state="        << state;       }
  if (requestOffline)       { ss << "&token_access_type=offline";     }
  return ss.str();
}

void DropboxAuth::exchangeCode(const string& code, const string& redirectUri) {
  unique_ptr<HttpRequest> req(httpFactory_->createHttpRequest(kTokenUrl));
  req->setMethod(HttpPostRequest);
  req->addParam("code",          code);
  req->addParam("grant_type",    "authorization_code");
  req->addParam("client_id",     clientId_);
  req->addParam("client_secret", clientSecret_);
  if (!redirectUri.empty()) {
    req->addParam("redirect_uri", redirectUri);
  }
  executeTokenRequest(req.get());
}

void DropboxAuth::refreshAccessToken() {
  if (refreshToken_.empty()) {
    throw DropboxException(CURL_ERROR,
      "refreshAccessToken: no refresh token available");
  }
  unique_ptr<HttpRequest> req(httpFactory_->createHttpRequest(kTokenUrl));
  req->setMethod(HttpPostRequest);
  req->addParam("grant_type",    "refresh_token");
  req->addParam("refresh_token", refreshToken_);
  req->addParam("client_id",     clientId_);
  req->addParam("client_secret", clientSecret_);
  executeTokenRequest(req.get());
}

void DropboxAuth::executeTokenRequest(http::HttpRequest* req) {
  if (req->execute() != 0) {
    throw DropboxException(CURL_ERROR, "Token request: curl transport error");
  }

  long httpCode = req->getResponseCode();
  if (httpCode != 200) {
    stringstream ss;
    ss << "Token request failed with HTTP " << httpCode;
    throw DropboxException(static_cast<DropboxErrorCode>(httpCode), ss.str());
  }

  string response(
    reinterpret_cast<char*>(req->getResponse()),
    req->getResponseSize());

  try {
    stringstream ss;
    ss << response;
    ptree pt;
    read_json(ss, pt);
    accessToken_  = pt.get<string>("access_token");
    refreshToken_ = pt.get<string>("refresh_token", "");
  } catch (exception& e) {
    throw DropboxException(MALFORMED_RESPONSE, e.what());
  }
}

bool DropboxAuth::canRefresh() const {
  return !refreshToken_.empty();
}

void DropboxAuth::addBearerHeader(HttpRequest* r) const {
  r->addHeader("Authorization", "Bearer " + accessToken_);
}

string DropboxAuth::getAccessToken()  const { return accessToken_;  }
string DropboxAuth::getRefreshToken() const { return refreshToken_; }

void DropboxAuth::setAccessToken(const string& token)  { accessToken_  = token; }
void DropboxAuth::setRefreshToken(const string& token) { refreshToken_ = token; }
