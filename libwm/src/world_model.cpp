/*
 * Copyright (c) 2014 Bernhard Firner
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 * or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

/******************************************************************************
 * Common world model components that will be the same regardless of the
 * database backend used.
 *****************************************************************************/
#include "world_model.hpp"
#include <iostream>
#include <mutex>
#include <utility>


using std::vector;
using std::u16string;
using world_model::Attribute;
using world_model::Buffer;
using world_model::URI;
using world_model::WorldState;


//TODO FIXME Make this a compiler option in the CMAKE script
//TODO FIXME Make this a class external to the world model
//TODO FIXME Use an existing logging facility
#define DEBUG

struct WMDebug {
};
template<typename T>
WMDebug& operator<<(WMDebug& dbg, T arg) {
  //Only print if DEBUG was defined during compilation
#ifdef DEBUG
  std::cerr<<arg;
#endif
  return dbg;
}

WMDebug debug;

WorldModel::world_state WorldModel::currentSnapshot(const URI& uri,
                                                    vector<u16string>& desired_attributes,
                                                    bool get_data) {
  //Return if nothing was requested
  if (desired_attributes.empty()) {
    return WorldModel::world_state();
  }
  //Find which URIs match the given search string
  std::vector<world_model::URI> matches = searchURI(uri);

  world_state result;
  
  if (0 < matches.size()) {
    //Make a regular expression for each attribute
    //First build the expressions
    std::vector<regex_t> expressions;
    for (auto exp_str = desired_attributes.begin(); exp_str != desired_attributes.end(); ++exp_str) {
      regex_t exp;
      int err = regcomp(&exp, std::string(exp_str->begin(), exp_str->end()).c_str(), REG_EXTENDED);
      if (0 != err) {
        debug<<"Error compiling regular expression "<<std::string(exp_str->begin(), exp_str->end())<<" in attribute of snapshot request.\n";
      }
      else {
        expressions.push_back(exp);
      }
    }
    //Flag the access control so that this read does not conflict with a write.
    SemaphoreFlag flag(access_control);
    
    //Find the attributes of interest for each URI
    //Attributes search have an AND relationship - this URI's results are only
    //returned if all of the attribute search have matches.
    for (auto uri_match = matches.begin(); uri_match != matches.end(); ++uri_match) {
      //Make a reference to the URI's attributes for ease of access
      std::vector<world_model::Attribute>& attributes = cur_state[*uri_match];
      std::vector<world_model::Attribute> matched_attributes;
      std::vector<bool> attr_matched(expressions.size());
      //Check each of this URI's attributes to see if it was requested
      for (auto attr = attributes.begin(); attr != attributes.end(); ++attr) {
        //This is a desired attribute if it appears in the attributes list
        //Also return this attribute if no attributes were specified
        //Check for a match that consumes the entire string
        //TODO Should also check origins here
        //Count which search expressions match
        bool matched = false;
        for (size_t search_ind = 0; search_ind < expressions.size(); ++search_ind) {
          //Use regex matching
          regmatch_t pmatch;
          int match = regexec(&expressions[search_ind], std::string(attr->name.begin(), attr->name.end()).c_str(), 1, &pmatch, 0);
          if (0 == match and 0 == pmatch.rm_so and attr->name.size() == pmatch.rm_eo) {
            attr_matched[search_ind] = true;
            matched = true;
          }
        }
        if (matched) {
          if (get_data) {
            matched_attributes.push_back(*attr);
          }
          else {
            matched_attributes.push_back(
                Attribute{attr->name, attr->creation_date, attr->expiration_date, attr->origin, Buffer{}});
          }
        }
      }
      //If all of the desired attributes were matched then return this URI
      //and its attributes to the user.
      if (std::none_of(attr_matched.begin(), attr_matched.end(), [&](const bool& b) { return not b;})) {
        result[*uri_match] = matched_attributes;
      }
    }
    //Free the memory used in the regex
    std::for_each(expressions.begin(), expressions.end(), [&](regex_t& exp) { regfree(&exp);});
  }

  return result;
}

//Register an attribute name as a transient type. Transient types are not
//stored in the SQL table but are stored in the cur_state map.
void WorldModel::registerTransient(std::u16string& attr_name, std::u16string& origin) {
  std::unique_lock<std::mutex> lck(transient_lock);
  transient.insert(std::make_pair(attr_name, origin));
}

/**
 * When this request is called the query object is immediately populated.
 * Afterwards any updates that arrive that match the query criteria are
 * added into the standing query.
 */
StandingQuery WorldModel::requestStandingQuery(const world_model::URI& uri,
                                                    std::vector<std::u16string>& desired_attributes, bool get_data) {
  StandingQuery sq(cur_state, uri, desired_attributes, get_data);
  debug<<"got a standing query\n";
  return sq;
}
