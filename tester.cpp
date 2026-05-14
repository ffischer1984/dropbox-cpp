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

#include <gtest/gtest.h>

#include <cstdlib>
#include <iostream>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>

#include "DropboxAccountInfo.h"
#include "DropboxMetadata.h"
#include "DropboxApi.h"

using namespace std;
using namespace dropbox;

// Globals
const string TEST_DIR  = "/testdir";
const size_t SIZE      = (1 << 20);
const size_t LARGE_SIZE = 2 * (1 << 20) + 2;
DropboxApi* d;

// ---------------------------------------------------------------------------
// Account info
// ---------------------------------------------------------------------------

TEST(AccountInfoTestCase, AccountInfoTest) {
  DropboxAccountInfo ac;
  DropboxErrorCode code = d->getAccountInfo(ac);

  EXPECT_EQ(SUCCESS, code);
  EXPECT_STREQ(getenv("DROPBOX_ACCOUNT_NAME"),  ac.getDisplayName().c_str());
  EXPECT_STREQ(getenv("DROPBOX_ACCOUNT_EMAIL"), ac.getEmail().c_str());
  // account_id starts with "dbid:" in v2
  EXPECT_EQ(0, ac.getUid().compare(0, 5, "dbid:"));
  // Quota should be populated by the second call inside getAccountInfo()
  EXPECT_GT(ac.getQuotaInfo().allocated, (uint64_t)0);
}

// ---------------------------------------------------------------------------
// Base fixture: create / tear down TEST_DIR
// ---------------------------------------------------------------------------

class BaseDropboxTestCase : public ::testing::Test {
protected:
  static void SetUpTestCase() {
    // Clean up any leftover folder from a previous aborted test run
    DropboxMetadata tmp;
    DropboxErrorCode delCode = d->deleteFile(TEST_DIR, tmp);
    if (delCode == SUCCESS) {
      cout << "[setup] Deleted leftover " << TEST_DIR << endl;
    }

    DropboxMetadata m;
    DropboxErrorCode code = d->createFolder(TEST_DIR, m);
    if (code != SUCCESS) {
      cerr << "[setup] createFolder(" << TEST_DIR << ") returned HTTP " << code << endl;
      cerr << "[setup] tag_=" << m.tag_ << " path_=" << m.path_ << endl;
    }
    assert(code == SUCCESS);
  }

  static void TearDownTestCase() {
    DropboxMetadata m;
    DropboxErrorCode code = d->deleteFile(TEST_DIR, m);
    assert(code == SUCCESS);
  }
};

// ---------------------------------------------------------------------------
// Folder operations
// ---------------------------------------------------------------------------

class DropboxFolderTestCase : public BaseDropboxTestCase {
public:
  void SetUp() {
    folderName_ = TEST_DIR + "/testdir";
    code_ = d->createFolder(folderName_, md_);
  }

  string folderName_;
  DropboxErrorCode code_;
  DropboxMetadata md_;
};

TEST_F(DropboxFolderTestCase, CreateFolderTest) {
  EXPECT_EQ(SUCCESS, code_);
  EXPECT_STREQ(folderName_.c_str(), md_.path_.c_str());
  EXPECT_TRUE(md_.isDir_);
  EXPECT_FALSE(md_.isDeleted_);
  // v2: tag_ must be "folder"
  EXPECT_STREQ("folder", md_.tag_.c_str());
}

TEST_F(DropboxFolderTestCase, DeleteFolderTest) {
  DropboxMetadata m;
  DropboxErrorCode code = d->deleteFile(folderName_, m);
  EXPECT_EQ(SUCCESS, code);
  EXPECT_STREQ(folderName_.c_str(), m.path_.c_str());
  EXPECT_TRUE(m.isDir_);
  // delete_v2 returns the metadata of the item as it was before deletion.
  // The ".tag" is "folder" (original type), NOT "deleted".
  EXPECT_FALSE(m.isDeleted_);
  EXPECT_STREQ("folder", m.tag_.c_str());
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

class DropboxFileTestCase : public BaseDropboxTestCase {
public:
  static uint8_t* getRandomData(size_t size) {
    int fd = open("/dev/urandom", O_RDONLY);
    assert(fd > 0);

    uint8_t* p = static_cast<uint8_t*>(malloc(size));
    assert(p);

    size_t rem = size, offset = 0;
    while (rem) {
      ssize_t ret = read(fd, p + offset, rem);
      assert(ret > 0);
      offset += static_cast<size_t>(ret);
      rem    -= static_cast<size_t>(ret);
    }
    close(fd);
    return p;
  }

  void SetUp() {
    fileName_ = TEST_DIR + "/testfile";
    DropboxUploadFileRequest up_req(fileName_);
    data_ = getRandomData(SIZE);
    up_req.setUploadData(data_, SIZE);
    code_ = d->uploadFile(up_req, md_);
  }

  void TearDown() {
    free(data_);
  }

  uint8_t* data_;
  string fileName_;
  DropboxErrorCode code_;
  DropboxMetadata md_;
};

// ---------------------------------------------------------------------------
// Upload / download / copy / move / delete
// ---------------------------------------------------------------------------

TEST_F(DropboxFileTestCase, UploadFileTest) {
  EXPECT_EQ(SUCCESS, code_);
  EXPECT_STREQ(fileName_.c_str(), md_.path_.c_str());
  EXPECT_FALSE(md_.isDir_);
  EXPECT_FALSE(md_.isDeleted_);
  EXPECT_EQ(SIZE, md_.sizeBytes_);
  EXPECT_FALSE(md_.rev_.empty());
}

TEST_F(DropboxFileTestCase, NonOverWriteTest) {
  // Upload to same path with overwrite=false → Dropbox auto-renames
  DropboxUploadFileRequest up_req(fileName_);
  auto* extra = getRandomData(SIZE);
  up_req.setUploadData(extra, SIZE);
  up_req.setOverwrite(false);

  DropboxMetadata m;
  DropboxErrorCode code = d->uploadFile(up_req, m);
  free(extra);

  EXPECT_EQ(code, SUCCESS);
  EXPECT_STRNE(fileName_.c_str(), m.path_.c_str()); // auto-renamed
  EXPECT_FALSE(m.isDir_);
  EXPECT_FALSE(m.isDeleted_);
  EXPECT_EQ(SIZE, m.sizeBytes_);
}

TEST_F(DropboxFileTestCase, CopyFileTest) {
  string copy_filename = fileName_ + ".bk";
  DropboxMetadata m;
  DropboxErrorCode code = d->copyFile(fileName_, copy_filename, m);

  EXPECT_EQ(code, SUCCESS);
  EXPECT_STREQ(copy_filename.c_str(), m.path_.c_str());
  EXPECT_EQ(md_.isDir_, m.isDir_);
  EXPECT_EQ(md_.sizeBytes_, m.sizeBytes_);
}

TEST_F(DropboxFileTestCase, MoveFileTest) {
  string copy_filename = fileName_ + ".bk2";
  DropboxMetadata m;
  DropboxErrorCode code = d->moveFile(fileName_, copy_filename, m);

  EXPECT_EQ(code, SUCCESS);
  EXPECT_STREQ(copy_filename.c_str(), m.path_.c_str());
  EXPECT_EQ(md_.isDir_, m.isDir_);
  EXPECT_EQ(md_.sizeBytes_, m.sizeBytes_);
}

TEST_F(DropboxFileTestCase, GetFileTest) {
  DropboxGetFileRequest gfreq(fileName_);
  DropboxGetFileResponse gfres;

  DropboxErrorCode code = d->getFile(gfreq, gfres);

  EXPECT_EQ(code, SUCCESS);
  EXPECT_EQ(SIZE, gfres.getDataLength());
  EXPECT_EQ(0, memcmp(data_, gfres.getData(), SIZE));

  DropboxMetadata m = gfres.getMetadata();
  EXPECT_STREQ(fileName_.c_str(), m.path_.c_str());
  EXPECT_FALSE(m.isDir_);
  EXPECT_FALSE(m.isDeleted_);
  EXPECT_EQ(md_.sizeBytes_, m.sizeBytes_);
}

TEST_F(DropboxFileTestCase, PartialGetFileTest) {
  DropboxGetFileRequest gfreq(fileName_);
  DropboxGetFileResponse gfres;
  const size_t offset = 1177;
  const size_t len    = 6656;

  gfreq.setRange(offset, len);
  DropboxErrorCode code = d->getFile(gfreq, gfres);

  EXPECT_EQ(code, PARTIAL_CONTENT);
  EXPECT_EQ(len, gfres.getDataLength());
  EXPECT_EQ(0, memcmp(data_ + offset, gfres.getData(), len));
}

TEST_F(DropboxFileTestCase, DeleteFileTest) {
  DropboxMetadata m;
  DropboxErrorCode code = d->deleteFile(fileName_, m);
  EXPECT_EQ(SUCCESS, code);
  EXPECT_STREQ(fileName_.c_str(), m.path_.c_str());
  EXPECT_FALSE(m.isDir_);
  // delete_v2 returns original item metadata (tag "file"), not "deleted"
  EXPECT_FALSE(m.isDeleted_);
}

// ---------------------------------------------------------------------------
// Large file upload (upload session)
// ---------------------------------------------------------------------------

class DropboxLargeFileTestCase : public BaseDropboxTestCase {
public:
  void SetUp() {
    fileName_ = TEST_DIR + "/largetestfile";
    data_     = DropboxFileTestCase::getRandomData(LARGE_SIZE);
    size_t rem_size = LARGE_SIZE;

    auto cb = [&](uint8_t* buf, size_t off, size_t sz) -> size_t {
      size_t fetched = (sz < rem_size) ? sz : rem_size;
      rem_size -= fetched;
      memcpy(buf, data_ + off, fetched);
      return fetched;
    };

    DropboxUploadLargeFileRequest req(fileName_, cb, true, "", SIZE, 0);
    code_ = d->uploadLargeFile(req, md_);
  }

  void TearDown() {
    free(data_);
  }

  uint8_t*         data_;
  string           fileName_;
  DropboxErrorCode code_;
  DropboxMetadata  md_;
};

TEST_F(DropboxLargeFileTestCase, UploadLargeFileTest) {
  EXPECT_EQ(SUCCESS, code_);
  EXPECT_STREQ(fileName_.c_str(), md_.path_.c_str());
  EXPECT_FALSE(md_.isDir_);
  EXPECT_FALSE(md_.isDeleted_);
  EXPECT_EQ(LARGE_SIZE, md_.sizeBytes_);
}

TEST_F(DropboxLargeFileTestCase, GetLargeFileTest) {
  DropboxGetFileRequest gfreq(fileName_);
  DropboxGetFileResponse gfres;

  DropboxErrorCode code = d->getFile(gfreq, gfres);

  EXPECT_EQ(code, SUCCESS);
  EXPECT_EQ(LARGE_SIZE, gfres.getDataLength());
  EXPECT_EQ(0, memcmp(data_, gfres.getData(), LARGE_SIZE));
}

// ---------------------------------------------------------------------------
// Metadata, revisions, search
// ---------------------------------------------------------------------------

class DropboxMetadataOpsTestCase : public BaseDropboxTestCase {
public:
  void SetUp() {
    fileName_ = TEST_DIR + "/testfile";
    DropboxUploadFileRequest up_req(fileName_);
    data_ = DropboxFileTestCase::getRandomData(SIZE);
    up_req.setUploadData(data_, SIZE);

    code_ = d->uploadFile(up_req, md_);
    assert(code_ == SUCCESS);

    code_ = d->copyFile(fileName_, fileName_ + "_1", md_);
    code_ = d->copyFile(fileName_, fileName_ + "_2", md_);
    savedRev_ = md_.rev_;
    code_ = d->deleteFile(fileName_ + "_2", md_);
  }

  void TearDown() {
    free(data_);
  }

  uint8_t* data_;
  string fileName_;
  DropboxErrorCode code_;
  DropboxMetadata md_;
  string savedRev_;
};

TEST_F(DropboxMetadataOpsTestCase, MetadataTest) {
  DropboxMetadataRequest req(TEST_DIR);
  DropboxMetadataResponse res;

  d->getFileMetadata(req, res);
  DropboxMetadata m = res.getMetadata();

  EXPECT_STREQ(TEST_DIR.c_str(), m.path_.c_str());
  EXPECT_TRUE(m.isDir_);
  // v2 has no 'root' concept – tag_ should be "folder"
  EXPECT_STREQ("folder", m.tag_.c_str());
}

TEST_F(DropboxMetadataOpsTestCase, MetadataListingTest) {
  DropboxMetadataRequest req(TEST_DIR, /*includeChildren=*/true);
  DropboxMetadataResponse res;

  d->getFileMetadata(req, res);
  const auto& children = res.getChildren();

  // testfile, testfile_1 (testfile_2 was deleted)
  EXPECT_EQ(2UL, children.size());
  for (const auto& m : children) {
    EXPECT_FALSE(m.isDir_);
    EXPECT_FALSE(m.isDeleted_);
  }
}

TEST_F(DropboxMetadataOpsTestCase, MetadataIncludeDeletesListingTest) {
  // list_folder with include_deleted=true
  DropboxMetadataRequest req(TEST_DIR, /*includeChildren=*/true, /*includeDeleted=*/true);
  DropboxMetadataResponse res;

  d->getFileMetadata(req, res);
  const auto& children = res.getChildren();

  // At least 3 entries (testfile, testfile_1, testfile_2 deleted)
  EXPECT_GE(children.size(), 3UL);

  bool foundDeleted = false;
  for (const auto& m : children) {
    if (m.path_ == fileName_ + "_2") {
      EXPECT_TRUE(m.isDeleted_);
      foundDeleted = true;
    }
  }
  EXPECT_TRUE(foundDeleted);
}

TEST_F(DropboxMetadataOpsTestCase, RevisionsTest) {
  DropboxRevisions revs;
  d->getRevisions(fileName_ + "_2", 10, revs);
  EXPECT_GE(revs.getRevisions().size(), 1UL);
}

TEST_F(DropboxMetadataOpsTestCase, RestoreTest) {
  DropboxMetadata m;
  DropboxErrorCode code = d->restoreFile(fileName_ + "_2", savedRev_, m);
  EXPECT_EQ(SUCCESS, code);

  DropboxGetFileRequest gfreq(fileName_ + "_2");
  DropboxGetFileResponse gfres;
  code = d->getFile(gfreq, gfres);

  EXPECT_EQ(SUCCESS, code);
  EXPECT_EQ(SIZE, gfres.getDataLength());
  EXPECT_EQ(0, memcmp(gfres.getData(), data_, SIZE));
}

TEST_F(DropboxMetadataOpsTestCase, SearchTest) {
  DropboxSearchRequest req(TEST_DIR, "testfile", false);
  DropboxSearchResult  res;

  DropboxErrorCode code = d->search(req, res);
  EXPECT_EQ(SUCCESS, code);
  // testfile and testfile_1 (2 non-deleted)
  EXPECT_EQ(2UL, res.getResults().size());
}

// ---------------------------------------------------------------------------
// Global test environment: set up the DropboxApi singleton
// ---------------------------------------------------------------------------

class DropboxTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    const char* api_key    = getenv("DROPBOX_API_KEY");
    const char* api_secret = getenv("DROPBOX_API_SECRET");

    if (!api_key || !api_secret) {
      GTEST_SKIP() << "Integration tests require DROPBOX_API_KEY and "
                      "DROPBOX_API_SECRET to be set. "
                      "See runtest.sh.skeleton for details.";
    }

    d = new DropboxApi(api_key, api_secret);

    const char* auth_token = getenv("DROPBOX_ACCESS_TOKEN");
    if (auth_token) {
      d->setAccessToken(auth_token);
      cout << "Using stored access token." << endl;
    } else {
      // Interactive OAuth 2.0 authorization code flow
      d->authenticate([](const string& url) -> string {
        cout << "\nAuthorize the app at:\n  " << url << "\n\n";
        cout << "Paste the authorization code here: ";
        string code;
        cin >> code;
        return code;
      });

      cout << "\nAccess token  : " << d->getAccessToken()  << endl;
      cout << "Refresh token : " << d->getRefreshToken() << endl;
      cout << "(Set DROPBOX_ACCESS_TOKEN to skip auth next time)\n\n";
    }
  }

  void TearDown() override {
    delete d;
  }
};

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new DropboxTestEnvironment());

  return RUN_ALL_TESTS();
}
