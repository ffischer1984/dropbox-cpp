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

#ifndef __DROPBOX_API_H__
#define __DROPBOX_API_H__

#include "util/DropboxAuth.h"
#include "util/HttpRequestFactory.h"
#include "util/HttpRequest.h"

#include "DropboxException.h"
#include "DropboxAccountInfo.h"
#include "DropboxMetadata.h"
#include "DropboxRevisions.h"
#include "DropboxGetFile.h"
#include "DropboxUploadFile.h"
#include "DropboxUploadLargeFile.h"
#include "DropboxSearch.h"

#include <string>
#include <memory>
#include <mutex>

namespace dropbox {

class DropboxApi {
public:
  /**
   * Create a DropboxApi instance without a pre-existing access token.
   * Call authenticate() to obtain one before making API calls.
   *
   * @param clientId      Your Dropbox app key.
   * @param clientSecret  Your Dropbox app secret.
   */
  DropboxApi(const std::string& clientId, const std::string& clientSecret);

  /**
   * Create a DropboxApi instance with a pre-existing access token.
   * The token is used immediately without requiring authenticate().
   *
   * @param clientId      Your Dropbox app key.
   * @param clientSecret  Your Dropbox app secret.
   * @param accessToken   A previously obtained OAuth 2.0 Bearer token.
   */
  DropboxApi(const std::string& clientId,
    const std::string& clientSecret,
    const std::string& accessToken);

  /**
   * Interactive OAuth 2.0 authorization code flow.
   *
   * The callback receives the authorization URL.  It should display the URL
   * to the user and return the authorization code the user receives after
   * granting access (from the redirect URI query param or the Dropbox
   * "show code" page).
   *
   * @param cb  Called with the auth URL; must return the authorization code.
   */
  void authenticate(std::function<std::string(const std::string& authUrl)> cb);

  /**
   * Set a pre-existing access token.
   *
   * @param token  OAuth 2.0 Bearer token.
   */
  void setAccessToken(const std::string& token);

  /** Return the current access token. */
  std::string getAccessToken() const;

  /**
   * Set a refresh token to enable automatic token renewal.
   *
   * @param token  OAuth 2.0 refresh token.
   */
  void setRefreshToken(const std::string& token);

  /** Return the current refresh token (empty if none). */
  std::string getRefreshToken() const;

  // -------------------------------------------------------------------------
  // Core API methods
  // -------------------------------------------------------------------------
  /**
   * Get account info for the authenticated user.
   * Internally calls both /2/users/get_current_account and
   * /2/users/get_space_usage to populate all DropboxAccountInfo fields.
   */
  DropboxErrorCode getAccountInfo(DropboxAccountInfo& info);

  /** Download a file.  Maps to /2/files/download. */
  DropboxErrorCode getFile(DropboxGetFileRequest& req,
    DropboxGetFileResponse& res);

  /**
   * Get metadata for a file or folder.
   * - If req.includeChildren() is false: calls /2/files/get_metadata.
   * - If req.includeChildren() is true:  calls /2/files/list_folder.
   *   Note: only the first page of results is returned.
   */
  DropboxErrorCode getFileMetadata(DropboxMetadataRequest& req,
    DropboxMetadataResponse& res);

  /** Get file revisions.  Maps to /2/files/list_revisions. */
  DropboxErrorCode getRevisions(const std::string& path,
    size_t numRevisions,
    DropboxRevisions& revs);

  /** Restore a file to a given revision.  Maps to /2/files/restore. */
  DropboxErrorCode restoreFile(const std::string& path,
    const std::string& rev,
    DropboxMetadata& m);

  /** Delete a file or folder.  Maps to /2/files/delete_v2. */
  DropboxErrorCode deleteFile(const std::string& path, DropboxMetadata& m);

  /** Copy a file or folder.  Maps to /2/files/copy_v2. */
  DropboxErrorCode copyFile(const std::string& from, const std::string& to,
    DropboxMetadata& m);

  /** Move a file or folder.  Maps to /2/files/move_v2. */
  DropboxErrorCode moveFile(const std::string& from, const std::string& to,
    DropboxMetadata& m);

  /** Create a folder.  Maps to /2/files/create_folder_v2. */
  DropboxErrorCode createFolder(const std::string& path, DropboxMetadata& m);

  /** Upload a small file (< 150 MB).  Maps to /2/files/upload. */
  DropboxErrorCode uploadFile(const DropboxUploadFileRequest& req,
    DropboxMetadata& res);

  /**
   * Upload a large file using the upload-session protocol.
   * Maps to /2/files/upload_session/start + append_v2 + finish.
   */
  DropboxErrorCode uploadLargeFile(const DropboxUploadLargeFileRequest& req,
    DropboxMetadata& res);

  /** Search for files.  Maps to /2/files/search_v2. */
  DropboxErrorCode search(const DropboxSearchRequest&, DropboxSearchResult&);

private:
  /** Helper: copy or move (shared logic for copyFile / moveFile). */
  DropboxErrorCode copyOrMove(const std::string& from,
    const std::string& to,
    const std::string& endpoint,
    DropboxMetadata& m);

  /** Execute a prepared request, adding the Bearer auth header. */
  DropboxErrorCode execute(std::shared_ptr<http::HttpRequest> r);

  /** Parse a v2 response that wraps the result metadata in a sub-key. */
  static void parseWrappedMetadata(const std::string& json,
    const std::string& key,
    DropboxMetadata& m);

  mutable std::mutex                stateLock_;
  std::unique_ptr<DropboxAuth>      auth_;
  http::HttpRequestFactory*         httpFactory_;
};

} // namespace dropbox
#endif
