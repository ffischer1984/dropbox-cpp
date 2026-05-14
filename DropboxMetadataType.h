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

#ifndef __DROPBOX_METADATA_TYPE_H__
#define __DROPBOX_METADATA_TYPE_H__

#include "DropboxException.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>

#include <sys/types.h>
#include <string>
#include <vector>

namespace dropbox {

const size_t DEFAULT_FILE_LIMIT = 10;

/**
 * Metadata for a file, folder, or deleted item as returned by Dropbox API v2.
 *
 * Key differences from API v1:
 *  - ".tag" field drives isDir_ / isDeleted_ (replaces the old bool fields).
 *  - path_ contains path_display (the canonical cased path).
 *  - hash_ is now the content_hash (SHA-256 block hash).
 *  - clientMtime_ is now clientModified_ (ISO-8601 timestamp string).
 *  - root_, sizeStr_, icon_, mimeType_, thumbExists_ are removed.
 *  - id_, name_, serverModified_ are new v2 fields.
 */
typedef struct DropboxMetadata {
  // v2 ".tag" value: "file", "folder", or "deleted"
  std::string         tag_;

  // Filename component only (e.g. "Prime_Numbers.txt")
  std::string         name_;

  // Full path as displayed (path_display), e.g. "/Homework/math/Prime_Numbers.txt"
  std::string         path_;

  // Stable unique ID for the item (survives renames within the same folder)
  std::string         id_;

  // File size in bytes (0 for folders and deleted items)
  size_t              sizeBytes_   = 0;

  // Convenience booleans derived from tag_
  bool                isDir_       = false;
  bool                isDeleted_   = false;

  // Revision string (files only; empty for folders)
  std::string         rev_;

  // Content hash (files only; Dropbox block-level SHA-256 hash)
  std::string         contentHash_;

  // Client-side modification timestamp (ISO-8601, e.g. "2015-05-12T15:50:38Z")
  std::string         clientModified_;

  // Server-side modification timestamp (ISO-8601)
  std::string         serverModified_;

  /**
   * Populate a DropboxMetadata from a single v2 metadata JSON object
   * (the object returned by /2/files/get_metadata, or one entry from
   *  the "entries" array of /2/files/list_folder).
   */
  static void readFromJson(boost::property_tree::ptree& pt,
      DropboxMetadata& m) {
    using namespace std;
    using boost::property_tree::ptree;
    // Boost property_tree uses '.' as a path separator, so we must use a
    // custom path_type with a NUL separator to read literal key ".tag".
    using Path = ptree::path_type;
    try {
      m.tag_  = pt.get<string>(Path(".tag", '\0'), "");
      m.isDir_     = (m.tag_ == "folder");
      m.isDeleted_ = (m.tag_ == "deleted");

      m.name_ = pt.get<string>("name",         "");
      m.path_ = pt.get<string>("path_display", "");
      m.id_   = pt.get<string>("id",           "");

      m.sizeBytes_      = pt.get<size_t>("size",             0);
      m.rev_            = pt.get<string>("rev",              "");
      m.contentHash_    = pt.get<string>("content_hash",     "");
      m.clientModified_ = pt.get<string>("client_modified",  "");
      m.serverModified_ = pt.get<string>("server_modified",  "");
    } catch (std::exception& e) {
      throw DropboxException(MALFORMED_RESPONSE, e.what());
    }
  }

  /**
   * Populate a vector from a ptree array of v2 metadata objects
   * (e.g. the "entries" node in a list_folder response).
   */
  static void readMetadataListFromJson(boost::property_tree::ptree& pt,
      std::vector<DropboxMetadata>& list) {
    BOOST_FOREACH(boost::property_tree::ptree::value_type& v, pt) {
      DropboxMetadata m;
      readFromJson(v.second, m);
      list.push_back(m);
    }
  }
} DropboxMetadata;

} // namespace dropbox
#endif
