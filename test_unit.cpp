/*
 * Unit tests for dropbox-cpp (Dropbox API v2).
 *
 * These tests are self-contained – they require no network access and no
 * external test framework.  Every test validates JSON parsing, data-model
 * construction, or helper logic using realistic v2 API response fixtures.
 *
 * Build & run:
 *   make unit_test
 *   ./unit_test
 *
 * Exit code: 0 = all passed, 1 = at least one failure.
 */

#include "DropboxAccountInfo.h"
#include "DropboxMetadata.h"
#include "DropboxMetadataType.h"
#include "DropboxRevisions.h"
#include "DropboxSearch.h"
#include "DropboxUploadFile.h"
#include "DropboxUploadLargeFile.h"
#include "DropboxGetFile.h"
#include "DropboxException.h"
#include "util/DropboxAuth.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <sstream>

using namespace dropbox;
using namespace std;

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int g_pass  = 0;
static int g_fail  = 0;
static int g_suite = 0;
static const char* g_current_suite = "";

#define SUITE(name) \
  do { g_current_suite = name; g_suite++; \
       printf("\n[%s]\n", name); } while (0)

#define CHECK(expr) \
  do { \
    if (expr) { \
      printf("  PASS  %s\n", #expr); \
      g_pass++; \
    } else { \
      printf("  FAIL  %s  (line %d)\n", #expr, __LINE__); \
      g_fail++; \
    } \
  } while (0)

#define CHECK_EQ(a, b) \
  do { \
    if ((a) == (b)) { \
      printf("  PASS  %s == %s\n", #a, #b); \
      g_pass++; \
    } else { \
      printf("  FAIL  (line %d) %s != %s\n", __LINE__, #a, #b); \
      g_fail++; \
    } \
  } while (0)

#define CHECK_STREQ(a, b) \
  do { \
    string _a = (a); string _b = (b); \
    if (_a == _b) { \
      printf("  PASS  \"%s\"\n", _a.c_str()); \
      g_pass++; \
    } else { \
      printf("  FAIL  (line %d) expected \"%s\" got \"%s\"\n", __LINE__, _b.c_str(), _a.c_str()); \
      g_fail++; \
    } \
  } while (0)

#define CHECK_THROWS(type, expr) \
  do { \
    bool _threw = false; \
    try { expr; } catch (const type&) { _threw = true; } catch (...) {} \
    if (_threw) { \
      printf("  PASS  throws " #type ": " #expr "\n"); g_pass++; \
    } else { \
      printf("  FAIL  did not throw " #type ": " #expr "  (line %d)\n", __LINE__); g_fail++; \
    } \
  } while (0)

// ---------------------------------------------------------------------------
// JSON fixture strings  (use tagged raw-string delimiter to avoid conflicts
// with the closing `)"` sequence that appears in string values like
// "Franz Ferdinand (Personal)")
// ---------------------------------------------------------------------------

// /2/files/get_metadata  – file
static const char* kFileMetaJson = R"json({
  ".tag": "file",
  "name": "Prime_Numbers.txt",
  "id": "id:a4ayc_80_OEAAAAAAAAAXw",
  "client_modified": "2015-05-12T15:50:38Z",
  "server_modified": "2015-05-12T15:50:38Z",
  "rev": "a1c10ce0dd78",
  "size": 7212,
  "path_lower": "/homework/math/prime_numbers.txt",
  "path_display": "/Homework/math/Prime_Numbers.txt",
  "content_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
})json";

// /2/files/get_metadata  – folder
static const char* kFolderMetaJson = R"json({
  ".tag": "folder",
  "name": "math",
  "id": "id:a4ayc_80_OEAAAAAAAAAXy",
  "path_lower": "/homework/math",
  "path_display": "/Homework/math"
})json";

// /2/files/get_metadata  – deleted
static const char* kDeletedMetaJson = R"json({
  ".tag": "deleted",
  "name": "math",
  "path_lower": "/homework/math",
  "path_display": "/Homework/math"
})json";

// /2/files/list_folder
static const char* kListFolderJson = R"json({
  "entries": [
    {
      ".tag": "file",
      "name": "Prime_Numbers.txt",
      "id": "id:abc",
      "client_modified": "2015-05-12T15:50:38Z",
      "server_modified": "2015-05-12T15:50:38Z",
      "rev": "rev1",
      "size": 7212,
      "path_lower": "/homework/math/prime_numbers.txt",
      "path_display": "/Homework/math/Prime_Numbers.txt",
      "content_hash": "deadbeef"
    },
    {
      ".tag": "folder",
      "name": "subdir",
      "id": "id:def",
      "path_lower": "/homework/math/subdir",
      "path_display": "/Homework/math/subdir"
    }
  ],
  "cursor": "ZtkX9_EHj3x7PMkVuFIhwKYXEpwpLwyxp9vMKomUhllil9q7eWiAu",
  "has_more": false
})json";

// /2/users/get_current_account
// Note: "display_name" contains ")" so we must use a tagged raw string.
static const char* kAccountJson = R"json({
  "account_id": "dbid:AAH4f99T0taONIb-OurWxbNQ6ywGRopQngc",
  "name": {
    "given_name": "Franz",
    "surname": "Ferdinand",
    "familiar_name": "Franz",
    "display_name": "Franz Ferdinand",
    "abbreviated_name": "FF"
  },
  "email": "franz@dropbox.com",
  "email_verified": true,
  "country": "US",
  "locale": "en",
  "is_paired": false,
  "account_type": { ".tag": "personal" },
  "root_info": { ".tag": "user", "root_namespace_id": "123" }
})json";

// /2/users/get_space_usage
static const char* kSpaceUsageJson = R"json({
  "used": 314159265,
  "allocation": {
    ".tag": "individual",
    "allocated": 10000000000
  }
})json";

// /2/files/list_revisions
static const char* kRevisionsJson = R"json({
  "is_deleted": false,
  "entries": [
    {
      ".tag": "file",
      "name": "Prime_Numbers.txt",
      "id": "id:abc",
      "client_modified": "2015-05-12T15:50:38Z",
      "server_modified": "2015-05-12T15:50:38Z",
      "rev": "rev2",
      "size": 9000,
      "path_lower": "/prime_numbers.txt",
      "path_display": "/Prime_Numbers.txt",
      "content_hash": "aabbcc"
    },
    {
      ".tag": "file",
      "name": "Prime_Numbers.txt",
      "id": "id:abc",
      "client_modified": "2015-04-10T10:00:00Z",
      "server_modified": "2015-04-10T10:00:00Z",
      "rev": "rev1",
      "size": 7212,
      "path_lower": "/prime_numbers.txt",
      "path_display": "/Prime_Numbers.txt",
      "content_hash": "112233"
    }
  ]
})json";

// /2/files/search_v2
static const char* kSearchJson = R"json({
  "matches": [
    {
      "match_type": { ".tag": "filename" },
      "metadata": {
        ".tag": "metadata",
        "metadata": {
          ".tag": "file",
          "name": "Prime_Numbers.txt",
          "id": "id:abc",
          "client_modified": "2015-05-12T15:50:38Z",
          "server_modified": "2015-05-12T15:50:38Z",
          "rev": "rev1",
          "size": 7212,
          "path_lower": "/homework/prime_numbers.txt",
          "path_display": "/Homework/Prime_Numbers.txt",
          "content_hash": "deadbeef"
        }
      }
    },
    {
      "match_type": { ".tag": "filename" },
      "metadata": {
        ".tag": "metadata",
        "metadata": {
          ".tag": "folder",
          "name": "Math",
          "id": "id:folder1",
          "path_lower": "/homework/math",
          "path_display": "/Homework/Math"
        }
      }
    }
  ],
  "has_more": false,
  "cursor": "cursor123"
})json";

// upload_session/start response
static const char* kSessionStartJson = R"json({"session_id": "1234faaf0678bcde"})json";

// upload_session/append_v2 cursor response
static const char* kSessionCursorJson = R"json({"session_id": "1234faaf0678bcde", "offset": 4194304})json";

// ---------------------------------------------------------------------------
// Test: DropboxMetadata – file
// ---------------------------------------------------------------------------
static void test_metadata_file() {
  SUITE("DropboxMetadata – file (.tag=file)");

  boost::property_tree::ptree pt;
  stringstream ss; ss << kFileMetaJson;
  boost::property_tree::json_parser::read_json(ss, pt);

  DropboxMetadata m;
  DropboxMetadata::readFromJson(pt, m);

  CHECK_STREQ(m.tag_,            "file");
  CHECK_STREQ(m.name_,           "Prime_Numbers.txt");
  CHECK_STREQ(m.path_,           "/Homework/math/Prime_Numbers.txt");
  CHECK_STREQ(m.id_,             "id:a4ayc_80_OEAAAAAAAAAXw");
  CHECK_EQ   (m.sizeBytes_,      (size_t)7212);
  CHECK      (!m.isDir_);
  CHECK      (!m.isDeleted_);
  CHECK_STREQ(m.rev_,            "a1c10ce0dd78");
  CHECK_STREQ(m.contentHash_,
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK_STREQ(m.clientModified_, "2015-05-12T15:50:38Z");
  CHECK_STREQ(m.serverModified_, "2015-05-12T15:50:38Z");
}

static void test_metadata_folder() {
  SUITE("DropboxMetadata – folder (.tag=folder)");

  boost::property_tree::ptree pt;
  stringstream ss; ss << kFolderMetaJson;
  boost::property_tree::json_parser::read_json(ss, pt);

  DropboxMetadata m;
  DropboxMetadata::readFromJson(pt, m);

  CHECK_STREQ(m.tag_,  "folder");
  CHECK_STREQ(m.name_, "math");
  CHECK_STREQ(m.path_, "/Homework/math");
  CHECK_EQ   (m.sizeBytes_, (size_t)0);
  CHECK      (m.isDir_);
  CHECK      (!m.isDeleted_);
  CHECK      (m.rev_.empty());
  CHECK      (m.contentHash_.empty());
}

static void test_metadata_deleted() {
  SUITE("DropboxMetadata – deleted (.tag=deleted)");

  boost::property_tree::ptree pt;
  stringstream ss; ss << kDeletedMetaJson;
  boost::property_tree::json_parser::read_json(ss, pt);

  DropboxMetadata m;
  DropboxMetadata::readFromJson(pt, m);

  CHECK_STREQ(m.tag_, "deleted");
  CHECK      (!m.isDir_);
  CHECK      (m.isDeleted_);
}

static void test_metadata_response_single() {
  SUITE("DropboxMetadataResponse – get_metadata (single item)");

  string json = kFileMetaJson;
  DropboxMetadataResponse res;
  res.readJson(json);

  const DropboxMetadata& m = res.getMetadata();
  CHECK_STREQ(m.tag_,  "file");
  CHECK_STREQ(m.path_, "/Homework/math/Prime_Numbers.txt");
  CHECK      (res.getChildren().empty());
}

static void test_metadata_response_list_folder() {
  SUITE("DropboxMetadataResponse – list_folder");

  string json = kListFolderJson;
  DropboxMetadataResponse res;
  res.readJson(json);

  const auto& children = res.getChildren();
  CHECK_EQ(children.size(), (size_t)2);

  CHECK_STREQ(children[0].tag_,  "file");
  CHECK_STREQ(children[0].name_, "Prime_Numbers.txt");
  CHECK      (!children[0].isDir_);

  CHECK_STREQ(children[1].tag_,  "folder");
  CHECK_STREQ(children[1].name_, "subdir");
  CHECK      (children[1].isDir_);
}

static void test_account_info() {
  SUITE("DropboxAccountInfo – get_current_account");

  string json = kAccountJson;
  DropboxAccountInfo info;
  info.readJson(json);

  CHECK_STREQ(info.getUid(),         "dbid:AAH4f99T0taONIb-OurWxbNQ6ywGRopQngc");
  CHECK_STREQ(info.getDisplayName(), "Franz Ferdinand");
  CHECK_STREQ(info.getEmail(),       "franz@dropbox.com");
  CHECK_STREQ(info.getCountry(),     "US");
  CHECK      (info.getReferralLink().empty());
  // quota not yet populated
  CHECK_EQ   (info.getQuotaInfo().used,      (uint64_t)0);
  CHECK_EQ   (info.getQuotaInfo().allocated, (uint64_t)0);
}

static void test_space_usage() {
  SUITE("DropboxAccountInfo – get_space_usage");

  string accountJson = kAccountJson;
  DropboxAccountInfo info;
  info.readJson(accountJson);

  string usageJson = kSpaceUsageJson;
  info.readSpaceUsageJson(usageJson);

  CHECK_EQ(info.getQuotaInfo().used,      (uint64_t)314159265);
  CHECK_EQ(info.getQuotaInfo().allocated, (uint64_t)10000000000ULL);
}

static void test_revisions() {
  SUITE("DropboxRevisions – list_revisions");

  string json = kRevisionsJson;
  DropboxRevisions revs;
  revs.readFromJson(json);

  auto& v = revs.getRevisions();
  CHECK_EQ(v.size(), (size_t)2);
  CHECK_STREQ(v[0].rev_,       "rev2");
  CHECK_EQ   (v[0].sizeBytes_, (size_t)9000);
  CHECK_STREQ(v[1].rev_,       "rev1");
  CHECK_EQ   (v[1].sizeBytes_, (size_t)7212);
}

static void test_search_result() {
  SUITE("DropboxSearchResult – search_v2 (2 matches)");

  string json = kSearchJson;
  DropboxSearchResult res = DropboxSearchResult::readFromJson(json);

  const auto& results = res.getResults();
  CHECK_EQ(results.size(), (size_t)2);

  CHECK_STREQ(results[0].tag_,      "file");
  CHECK_STREQ(results[0].name_,     "Prime_Numbers.txt");
  CHECK_EQ   (results[0].sizeBytes_, (size_t)7212);

  CHECK_STREQ(results[1].tag_,  "folder");
  CHECK_STREQ(results[1].name_, "Math");
  CHECK      (results[1].isDir_);
}

static void test_search_empty() {
  SUITE("DropboxSearchResult – empty result set");

  string json = R"json({"matches":[],"has_more":false})json";
  DropboxSearchResult res = DropboxSearchResult::readFromJson(json);
  CHECK(res.getResults().empty());
}

static void test_upload_session_start() {
  SUITE("DropboxUploadSessionCursor – upload_session/start");

  string json = kSessionStartJson;
  auto cursor = DropboxUploadSessionCursor::readFromJson(json);

  CHECK_STREQ(cursor.getSessionId(), "1234faaf0678bcde");
  CHECK_EQ   (cursor.getOffset(),    (size_t)0);
}

static void test_upload_session_cursor() {
  SUITE("DropboxUploadSessionCursor – append_v2 cursor");

  string json = kSessionCursorJson;
  auto cursor = DropboxUploadSessionCursor::readFromJson(json);

  CHECK_STREQ(cursor.getSessionId(), "1234faaf0678bcde");
  CHECK_EQ   (cursor.getOffset(),    (size_t)4194304);
}

static void test_auth_url() {
  SUITE("DropboxAuth – getAuthorizationUrl");

  DropboxAuth auth("my_app_key", "my_app_secret");

  string url = auth.getAuthorizationUrl();
  CHECK(url.find("https://www.dropbox.com/oauth2/authorize") != string::npos);
  CHECK(url.find("client_id=my_app_key") != string::npos);
  CHECK(url.find("response_type=code") != string::npos);
  CHECK(url.find("redirect_uri") == string::npos);

  string urlR = auth.getAuthorizationUrl("https://localhost");
  CHECK(urlR.find("redirect_uri=https://localhost") != string::npos);

  string urlS = auth.getAuthorizationUrl("", "csrf_token_xyz");
  CHECK(urlS.find("state=csrf_token_xyz") != string::npos);

  string urlO = auth.getAuthorizationUrl("", "", /*offline=*/true);
  CHECK(urlO.find("token_access_type=offline") != string::npos);

  string urlN = auth.getAuthorizationUrl("", "", /*offline=*/false);
  CHECK(urlN.find("token_access_type") == string::npos);
}

static void test_auth_token_management() {
  SUITE("DropboxAuth – token get/set");

  DropboxAuth auth("key", "secret");

  CHECK(auth.getAccessToken().empty());
  CHECK(auth.getRefreshToken().empty());
  CHECK(!auth.canRefresh());

  auth.setAccessToken("sl.u.access_token_123");
  CHECK_STREQ(auth.getAccessToken(), "sl.u.access_token_123");
  CHECK(!auth.canRefresh());

  auth.setRefreshToken("refresh_token_abc");
  CHECK_STREQ(auth.getRefreshToken(), "refresh_token_abc");
  CHECK(auth.canRefresh());
}

static void test_exception() {
  SUITE("DropboxException – error code and message");

  DropboxException ex(MALFORMED_RESPONSE, "bad JSON");
  CHECK(ex.getErrorCode() == MALFORMED_RESPONSE);
  CHECK_STREQ(string(ex.what()), "bad JSON");

  DropboxException ex2(CURL_ERROR, "connection refused");
  CHECK(ex2.getErrorCode() == CURL_ERROR);
  CHECK_STREQ(string(ex2.what()), "connection refused");
}

static void test_malformed_json() {
  SUITE("Malformed JSON -> DropboxException");

  CHECK_THROWS(DropboxException, ({
    string bad = "{ this is not valid json }}}";
    DropboxMetadataResponse res;
    res.readJson(bad);
  }));

  CHECK_THROWS(DropboxException, ({
    // Valid JSON but missing mandatory "account_id" field
    string bad = R"json({"email":"x@x.com"})json";
    DropboxAccountInfo info;
    info.readJson(bad);
  }));
}

static void test_metadata_list() {
  SUITE("DropboxMetadata::readMetadataListFromJson – bare array");

  string arrayJson = R"json([
    {".tag":"file","name":"a.txt","id":"id:1","size":100,
     "path_display":"/a.txt","path_lower":"/a.txt",
     "client_modified":"2020-01-01T00:00:00Z",
     "server_modified":"2020-01-01T00:00:00Z","rev":"r1","content_hash":"h1"},
    {".tag":"folder","name":"subdir","id":"id:2",
     "path_display":"/subdir","path_lower":"/subdir"}
  ])json";

  boost::property_tree::ptree pt;
  stringstream ss; ss << arrayJson;
  boost::property_tree::json_parser::read_json(ss, pt);

  vector<DropboxMetadata> list;
  DropboxMetadata::readMetadataListFromJson(pt, list);

  CHECK_EQ(list.size(), (size_t)2);
  CHECK_STREQ(list[0].name_, "a.txt");
  CHECK      (!list[0].isDir_);
  CHECK_STREQ(list[1].name_, "subdir");
  CHECK      (list[1].isDir_);
}

static void test_search_request() {
  SUITE("DropboxSearchRequest – accessors");

  DropboxSearchRequest req("/homework", "prime", true, 42);
  CHECK_STREQ(req.getSearchPath(),  "/homework");
  CHECK_STREQ(req.getSearchQuery(), "prime");
  CHECK_EQ   (req.getResultLimit(), (size_t)42);
  CHECK      (req.shouldIncludeDeleted());

  DropboxSearchRequest req2("/", "hello", false);
  CHECK(!req2.shouldIncludeDeleted());
  CHECK_EQ(req2.getResultLimit(), (size_t)100);
}

static void test_get_file_request() {
  SUITE("DropboxGetFileRequest – range");

  DropboxGetFileRequest req1("/path/to/file.txt");
  CHECK_STREQ(req1.getPath(), "/path/to/file.txt");
  CHECK      (req1.getRev().empty());
  CHECK      (!req1.hasRange());

  DropboxGetFileRequest req2("/path/file.txt", "rev_abc");
  CHECK_STREQ(req2.getRev(), "rev_abc");

  DropboxGetFileRequest req3("/path/file.txt");
  req3.setRange(1024, 4096);
  CHECK      (req3.hasRange());
  CHECK_EQ   (req3.getOffset(), (uint64_t)1024);
  CHECK_EQ   (req3.getLength(), (uint64_t)4096);
}

static void test_upload_file_request() {
  SUITE("DropboxUploadFileRequest – options");

  DropboxUploadFileRequest req("/path/file.txt");
  CHECK_STREQ(req.getPath(),        "/path/file.txt");
  CHECK      (req.shouldOverwrite());
  CHECK      (req.getParentRev().empty());
  CHECK      (req.getUploadData() == nullptr);
  CHECK_EQ   (req.getUploadDataSize(), (size_t)0);

  uint8_t data[] = {1, 2, 3};
  req.setUploadData(data, 3);
  req.setOverwrite(false);
  req.setParentRev("rev_xyz");

  CHECK      (!req.shouldOverwrite());
  CHECK_STREQ(req.getParentRev(), "rev_xyz");
  CHECK_EQ   (req.getUploadDataSize(), (size_t)3);
}

static void test_metadata_request() {
  SUITE("DropboxMetadataRequest – accessors");

  DropboxMetadataRequest req("/homework");
  CHECK_STREQ(req.path(),          "/homework");
  CHECK      (!req.includeChildren());
  CHECK      (!req.includeDeleted());
  CHECK_EQ   (req.getLimit(), DEFAULT_FILE_LIMIT);

  DropboxMetadataRequest req2("/homework", true, true);
  CHECK(req2.includeChildren());
  CHECK(req2.includeDeleted());

  req2.setLimit(50);
  CHECK_EQ(req2.getLimit(), (size_t)50);

  req2.setHash("abc123");
  CHECK_STREQ(req2.getHash(), "abc123");

  req2.setRev("rev99");
  CHECK_STREQ(req2.getRev(), "rev99");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  printf("dropbox-cpp unit tests\n");
  printf("======================\n");

  test_metadata_file();
  test_metadata_folder();
  test_metadata_deleted();
  test_metadata_response_single();
  test_metadata_response_list_folder();
  test_account_info();
  test_space_usage();
  test_revisions();
  test_search_result();
  test_search_empty();
  test_upload_session_start();
  test_upload_session_cursor();
  test_auth_url();
  test_auth_token_management();
  test_exception();
  test_malformed_json();
  test_metadata_list();
  test_search_request();
  test_get_file_request();
  test_upload_file_request();
  test_metadata_request();

  printf("\n======================\n");
  printf("Results: %d passed, %d failed  (%d suites)\n",
         g_pass, g_fail, g_suite);

  return g_fail == 0 ? 0 : 1;
}
