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

#include "DropboxAccountInfo.h"
#include "DropboxException.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <sstream>

using namespace dropbox;
using namespace std;
using namespace boost::property_tree;
using namespace boost::property_tree::json_parser;

/**
 * Parse /2/users/get_current_account response.
 * {
 *   "account_id": "dbid:AAH4f...",
 *   "name": { "display_name": "Franz Ferdinand", ... },
 *   "email": "franz@dropbox.com",
 *   "country": "US",
 *   ...
 * }
 */
void DropboxAccountInfo::readFromJson(DropboxAccountInfo* info, string& json) {
  try {
    stringstream ss;
    ss << json;

    ptree pt;
    read_json(ss, pt);

    info->uid_         = pt.get<string>("account_id");
    info->displayName_ = pt.get<string>("name.display_name");
    info->email_       = pt.get<string>("email");
    info->country_     = pt.get<string>("country", "");
  } catch (exception& e) {
    throw DropboxException(MALFORMED_RESPONSE, e.what());
  }
}

/**
 * Parse /2/users/get_space_usage response.
 * {
 *   "used": 314159265,
 *   "allocation": { ".tag": "individual", "allocated": 10000000000 }
 * }
 */
void DropboxAccountInfo::readSpaceUsageFromJson(DropboxAccountInfo* info,
    string& json) {
  try {
    stringstream ss;
    ss << json;

    ptree pt;
    read_json(ss, pt);

    info->quotaInfo_.used      = pt.get<uint64_t>("used",                  0);
    info->quotaInfo_.allocated = pt.get<uint64_t>("allocation.allocated",  0);
  } catch (exception& e) {
    throw DropboxException(MALFORMED_RESPONSE, e.what());
  }
}

DropboxAccountInfo::DropboxAccountInfo(string& json) {
  readFromJson(this, json);
}

void DropboxAccountInfo::readJson(string& json) {
  readFromJson(this, json);
}

void DropboxAccountInfo::readSpaceUsageJson(string& json) {
  readSpaceUsageFromJson(this, json);
}

string DropboxAccountInfo::getReferralLink() const {
  return "";  // Removed in Dropbox API v2
}

string DropboxAccountInfo::getDisplayName() const {
  return displayName_;
}

string DropboxAccountInfo::getUid() const {
  return uid_;
}

string DropboxAccountInfo::getCountry() const {
  return country_;
}

string DropboxAccountInfo::getEmail() const {
  return email_;
}

DropboxQuotaInfo DropboxAccountInfo::getQuotaInfo() const {
  return quotaInfo_;
}
