/*
 * Dropbox API v2 – OAuth 2.0 authentication helper.
 * Replaces the OAuth 1.0 util/OAuth.h used by the v1 API.
 *
 * Flow (authorization code, suitable for installed apps / devices):
 *   1. Call getAuthorizationUrl() and display the URL to the user.
 *   2. User visits the URL in a browser and authorizes the app.
 *   3. User receives an authorization code (from the redirect or the
 *      Dropbox "show code" page when redirect_uri is not used).
 *   4. Call exchangeCode(code) to obtain an access token.
 *   5. Optionally persist the refresh token and call
 *      refreshAccessToken() to renew access without user interaction.
 */

#ifndef __DROPBOX_AUTH_H__
#define __DROPBOX_AUTH_H__

#include "util/HttpRequestFactory.h"
#include "util/HttpRequest.h"

#include <string>

namespace dropbox {

class DropboxAuth {
public:
  /**
   * Create a DropboxAuth instance.
   *
   * @param clientId      Your Dropbox app key (from the App Console).
   * @param clientSecret  Your Dropbox app secret.
   */
  DropboxAuth(const std::string& clientId, const std::string& clientSecret);

  /**
   * Build the authorization URL the user must visit to grant access.
   *
   * @param redirectUri   URI Dropbox redirects to after authorization.
   *                      Use "https://localhost" for installed apps or leave
   *                      empty ("") to use the Dropbox "show code" page.
   * @param state         Optional CSRF state token (recommended).
   * @param requestOffline  If true, request a refresh token (offline access).
   *
   * @return  The authorization URL as a string.
   */
  std::string getAuthorizationUrl(
    const std::string& redirectUri   = "",
    const std::string& state         = "",
    bool               requestOffline = false) const;

  /**
   * Exchange an authorization code for an access token (and optionally a
   * refresh token if offline access was requested).
   *
   * Throws DropboxException on HTTP or parse errors.
   *
   * @param code         The authorization code from the redirect / show-code page.
   * @param redirectUri  Must match the value used in getAuthorizationUrl().
   */
  void exchangeCode(const std::string& code,
                    const std::string& redirectUri = "");

  /**
   * Refresh the access token using the stored refresh token.
   * Requires that a refresh token is available (requestOffline must have
   * been true during the initial authorize step).
   *
   * Throws DropboxException if no refresh token is set or on HTTP error.
   */
  void refreshAccessToken();

  /** Returns true if a refresh token has been set. */
  bool canRefresh() const;

  /**
   * Add the Authorization: Bearer <token> header to an existing request.
   * Called automatically by DropboxApi::execute().
   */
  void addBearerHeader(http::HttpRequest* r) const;

  std::string getAccessToken()  const;
  std::string getRefreshToken() const;

  void setAccessToken(const std::string& token);
  void setRefreshToken(const std::string& token);

private:
  /**
   * Execute a pre-built token-endpoint request and parse the JSON response.
   * Populates accessToken_ (and refreshToken_ if present).
   */
  void executeTokenRequest(http::HttpRequest* req);

  const std::string         clientId_;
  const std::string         clientSecret_;
  std::string               accessToken_;
  std::string               refreshToken_;
  http::HttpRequestFactory* httpFactory_;
};

} // namespace dropbox

#endif // __DROPBOX_AUTH_H__


