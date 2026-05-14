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

#ifndef __DROPBOX_UPLOAD_LARGE_FILE_H__
#define __DROPBOX_UPLOAD_LARGE_FILE_H__

#include <string>
#include <functional>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace dropbox {

/**
 * Request parameters for uploadLargeFile().
 *
 * The callback (dataCb_) is invoked repeatedly to retrieve chunks of the
 * file data.  It receives (buffer, offset, maxSize) and must return the
 * number of bytes written into buffer (0 = end of data, <0 = error).
 *
 * Dropbox API v2 uses a three-step upload-session protocol:
 *   1. upload_session/start  – open session, upload first chunk
 *   2. upload_session/append_v2 – upload subsequent chunks
 *   3. upload_session/finish – commit with destination path & mode
 *
 * Chunk size recommendation: 150 MB max per chunk; default is 4 MB.
 */
class DropboxUploadLargeFileRequest {
public:
  DropboxUploadLargeFileRequest(const std::string path,
    std::function<size_t(uint8_t*, size_t, size_t)> cb, // data callback
    const bool overwrite = true,
    const std::string parent_rev = "",
    size_t chunkSize = (1UL << 22),  // 4 MB default
    size_t offset = 0) :
      path_(path),
      dataCb_(cb),
      overwrite_(overwrite),
      parentRev_(parent_rev),
      chunkSize_(chunkSize),
      offset_(offset) {
  }

  void setOverwrite(bool overwrite)               { overwrite_  = overwrite;  }
  void setParentRev(const std::string parent_rev) { parentRev_  = parent_rev; }
  void setOffset(size_t offset)                   { offset_     = offset;     }

  std::string getPath()       const { return path_;       }
  bool shouldOverwrite()      const { return overwrite_;   }
  std::string getParentRev()  const { return parentRev_;   }
  size_t getChunkSize()       const { return chunkSize_;   }
  size_t getOffset()          const { return offset_;      }

  size_t getData(uint8_t* data, size_t offset, size_t size) const {
    return dataCb_(data, offset, size);
  }

private:
  const std::string                               path_;
  std::function<size_t(uint8_t*, size_t, size_t)> dataCb_;
  bool                                            overwrite_;
  std::string                                     parentRev_;
  size_t                                          chunkSize_;
  size_t                                          offset_;
};

/**
 * Response from upload_session/start and upload_session/append_v2.
 *
 * v2 cursor:
 * { "session_id": "1234faaf0678bcde", "offset": 4194304 }
 *
 * (v1 had upload_id + expires; replaced by session_id in v2.)
 */
class DropboxUploadSessionCursor {
public:
  static DropboxUploadSessionCursor readFromJson(const std::string& response) {
    using namespace boost::property_tree;
    using namespace boost::property_tree::json_parser;
    using namespace std;

    stringstream s;
    s << response;

    ptree pt;
    read_json(s, pt);

    string sessionId = pt.get<string>("session_id");
    size_t offset    = pt.get<size_t>("offset", 0);

    return DropboxUploadSessionCursor(sessionId, offset);
  }

  std::string getSessionId() const { return sessionId_; }
  size_t      getOffset()    const { return offset_;     }

private:
  DropboxUploadSessionCursor(std::string sessionId, size_t offset) :
    sessionId_(sessionId), offset_(offset) {
  }

  const std::string sessionId_;
  const size_t      offset_;
};

// Backward-compatible alias (v1 code referenced DropboxUploadLargeFileResponse)
using DropboxUploadLargeFileResponse = DropboxUploadSessionCursor;

} // namespace dropbox
#endif
