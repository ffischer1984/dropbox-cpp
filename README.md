dropbox-cpp
===========

[![CI](https://github.com/rahuliyer/dropbox-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/rahuliyer/dropbox-cpp/actions/workflows/ci.yml)

dropbox-cpp is a C++ client for the **Dropbox API v2**.

> **Migrated from API v1 to API v2** — see the [Migration Guide](#migration-guide-v1--v2) below if you are upgrading existing code.

---

Building the library
--------------------
Requires **libcurl** and **Boost** (property\_tree / json\_parser).

On macOS with Homebrew:
```
brew install boost
```

Then build:
```
make clean
make
```

Running the tests
-----------------
Copy the test runner skeleton and fill in your credentials:
```
cp runtest.sh.skeleton runtest.sh
```

Edit `runtest.sh`:

| Variable | What to set |
|---|---|
| `DROPBOX_APP_KEY` | Your Dropbox app key (from the [App Console](https://www.dropbox.com/developers/apps)) |
| `DROPBOX_APP_SECRET` | Your Dropbox app secret |
| `DROPBOX_ACCESS_TOKEN` | *(Optional)* A pre-existing OAuth 2.0 Bearer token |

```
./runtest.sh
```

If no access token is provided, the test suite will print an authorization URL. Visit that URL in a browser, grant access, and paste the authorization code back into the terminal.

---

Using the library
-----------------

### 1. Instantiate `DropboxApi`

```cpp
#include "DropboxApi.h"
using namespace dropbox;

// Without a pre-existing token (you will call authenticate() next)
DropboxApi d("my_app_key", "my_app_secret");

// With a pre-existing Bearer token (skip authenticate())
DropboxApi d("my_app_key", "my_app_secret", "sl.u.my_access_token");
```

### 2. Authenticate (OAuth 2.0 Authorization Code Flow)

The `authenticate()` callback receives the authorization URL and must return
the authorization code the user obtains after granting access.

```cpp
// Simple interactive example
std::string code = d.authenticate([](const std::string& url) -> std::string {
    std::cout << "Visit this URL to authorize:\n" << url << "\n";
    std::cout << "Paste the authorization code: ";
    std::string code;
    std::cin >> code;
    return code;
});
```

Persist the tokens for future runs to avoid re-authorization:
```cpp
// Save after first auth
std::string accessToken  = d.getAccessToken();   // persist this
std::string refreshToken = d.getRefreshToken();  // persist this (enables silent renewal)

// Restore on the next run
d.setAccessToken(accessToken);
d.setRefreshToken(refreshToken);  // allows automatic token refresh
```

### 3. API calls

All methods return a `DropboxErrorCode`. `SUCCESS` (200) indicates success.

```cpp
// Account info (calls /2/users/get_current_account + /2/users/get_space_usage)
DropboxAccountInfo info;
DropboxErrorCode code = d.getAccountInfo(info);
if (code == SUCCESS) {
    std::cout << info.getDisplayName() << "\n";
    std::cout << "Used: " << info.getQuotaInfo().used << " bytes\n";
}

// Download a file
DropboxGetFileRequest req("/path/to/file.txt");
DropboxGetFileResponse res;
d.getFile(req, res);

// Get metadata for a file
DropboxMetadataRequest metaReq("/path/to/file.txt");
DropboxMetadataResponse metaRes;
d.getFileMetadata(metaReq, metaRes);
DropboxMetadata& m = metaRes.getMetadata();
std::cout << m.path_ << "  (" << m.sizeBytes_ << " bytes)\n";

// List a folder
DropboxMetadataRequest folderReq("/my-folder", /*includeChildren=*/true);
DropboxMetadataResponse folderRes;
d.getFileMetadata(folderReq, folderRes);
for (auto& child : folderRes.getChildren()) {
    std::cout << child.path_ << (child.isDir_ ? "/" : "") << "\n";
}

// Upload a small file (< 150 MB)
uint8_t data[] = { /* ... */ };
DropboxUploadFileRequest upReq("/remote/path.txt");
upReq.setUploadData(data, sizeof(data));
DropboxMetadata uploaded;
d.uploadFile(upReq, uploaded);

// Delete
DropboxMetadata deleted;
d.deleteFile("/remote/path.txt", deleted);

// Copy / Move
DropboxMetadata result;
d.copyFile("/src.txt", "/dst.txt", result);
d.moveFile("/old.txt", "/new.txt", result);

// Create folder
DropboxMetadata folder;
d.createFolder("/new-folder", folder);

// Search
DropboxSearchRequest searchReq("/", "quarterly report", false, 50);
DropboxSearchResult  searchRes;
d.search(searchReq, searchRes);
for (auto& r : searchRes.getResults()) {
    std::cout << r.path_ << "\n";
}
```

---

Migration Guide: v1 → v2
-------------------------

### Authentication — OAuth 1.0 → OAuth 2.0

| v1 | v2 |
|---|---|
| `DropboxApi d(key, secret)` | `DropboxApi d(key, secret)` *(same)* |
| `DropboxApi d(key, secret, token, tokenSecret)` | `DropboxApi d(key, secret, token)` — no token secret |
| `d.setAccessToken(token, secret)` | `d.setAccessToken(token)` — no secret |
| `d.getAccessTokenSecret()` | *removed* |
| `d.setRoot("dropbox")` | *removed* — no root concept in v2 |
| Callback: `void(string reqToken, string reqSecret)` | Callback: `string(const string& authUrl)` → return auth code |

**Before (v1):**
```cpp
d.authenticate([](string token, string secret) {
    cout << "Authorize at https://www.dropbox.com/1/oauth/authorize?oauth_token=" << token;
    cin.get();
});
```

**After (v2):**
```cpp
d.authenticate([](const string& url) -> string {
    cout << "Authorize at " << url << "\n";
    string code; cin >> code;
    return code;
});
```

### `DropboxMetadata` struct

| v1 field | v2 field | Notes |
|---|---|---|
| `path_` | `path_` | Now contains `path_display` (canonical casing) |
| `sizeBytes_` | `sizeBytes_` | Unchanged |
| `isDir_` | `isDir_` | Now derived from `.tag == "folder"` |
| `isDeleted_` | `isDeleted_` | Now derived from `.tag == "deleted"` |
| `rev_` | `rev_` | Unchanged |
| `hash_` | `contentHash_` | Renamed; now Dropbox block-level SHA-256 |
| `clientMtime_` | `clientModified_` | Renamed; ISO-8601 string |
| *(new)* | `tag_` | `"file"`, `"folder"`, or `"deleted"` |
| *(new)* | `id_` | Stable file ID (survives renames) |
| *(new)* | `name_` | Filename component only |
| *(new)* | `serverModified_` | Server-side modification time |
| `sizeStr_` | *removed* | v2 does not return human-readable sizes |
| `root_` | *removed* | No root concept in v2 |
| `icon_` | *removed* | |
| `mimeType_` | *removed* | |
| `thumbExists_` | *removed* | |

### `DropboxAccountInfo`

| v1 | v2 |
|---|---|
| `getUid()` → numeric uid string | `getUid()` → `account_id` (e.g. `"dbid:AAH4f…"`) |
| `getDisplayName()` | unchanged |
| `getReferralLink()` | always returns `""` (removed in v2) |
| `getQuotaInfo().quota` | `getQuotaInfo().allocated` |
| `getQuotaInfo().normal` | `getQuotaInfo().used` |
| `getQuotaInfo().shared` | *removed* (always 0) |

### `DropboxUploadLargeFileResponse`

The v1 `upload_id` / `expires` response is replaced by the v2 upload session
cursor. `DropboxUploadLargeFileResponse` is now a type alias for
`DropboxUploadSessionCursor`:

| v1 | v2 |
|---|---|
| `getUploadId()` | `getSessionId()` |
| `getOffset()` | `getOffset()` — unchanged |
| `getExpiry()` | *removed* |

### API endpoint mapping

| Method | v1 endpoint | v2 endpoint |
|---|---|---|
| `getAccountInfo()` | `GET /1/account/info` | `POST /2/users/get_current_account` + `get_space_usage` |
| `getFile()` | `GET /files/{root}/{path}` | `POST /2/files/download` (path in `Dropbox-API-Arg` header) |
| `getFileMetadata()` | `GET /1/metadata/{root}/{path}` | `POST /2/files/get_metadata` or `list_folder` |
| `getRevisions()` | `GET /1/revisions/{root}/{path}` | `POST /2/files/list_revisions` |
| `restoreFile()` | `POST /1/restore/{root}/{path}` | `POST /2/files/restore` |
| `deleteFile()` | `POST /1/fileops/delete` | `POST /2/files/delete_v2` |
| `copyFile()` | `POST /1/fileops/copy` | `POST /2/files/copy_v2` |
| `moveFile()` | `POST /1/fileops/move` | `POST /2/files/move_v2` |
| `createFolder()` | `POST /1/fileops/create_folder` | `POST /2/files/create_folder_v2` |
| `uploadFile()` | `PUT /1/files_put/{root}/{path}` | `POST /2/files/upload` (binary body, path in header) |
| `uploadLargeFile()` | `PUT /1/chunked_upload` + `commit_chunked_upload` | `POST /2/files/upload_session/start` + `append_v2` + `finish` |
| `search()` | `GET /1/search/{root}/{path}` | `POST /2/files/search_v2` |

---

For the full API reference see `DropboxApi.h`.


## Sponsor me
[![paypal](https://www.paypalobjects.com/en_US/DK/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=EN22Z95HKGD74&source=url)
[![buymeacoffee](https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png)](https://buymeacoffee.com/CWkjUYH)