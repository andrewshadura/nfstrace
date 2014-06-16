//------------------------------------------------------------------------------
// Author: Dzianis Huznou
// Description: Unique Plugin-API interface header.
// Aggregated all definitions for plugins' development
// Copyright (c) 2013 EPAM Systems
//------------------------------------------------------------------------------
/*
    This file is part of Nfstrace.

    Nfstrace is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2 of the License.

    Nfstrace is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Nfstrace.  If not, see <http://www.gnu.org/licenses/>.
*/
//------------------------------------------------------------------------------
#ifndef PLUGIN_API_H
#define PLUGIN_API_H
//------------------------------------------------------------------------------
#include <iostream>

#include "fh.h"
#include "ianalyzer_type.h"
#include "nfs3_types.h"
#include "nfs3_types_rpcgen.h"
//------------------------------------------------------------------------------
using namespace NST::API;
//------------------------------------------------------------------------------
extern "C"
{
// THESE CALLS MUST BE IMPLEMENTED BY Pluggable Analysis Module
const char* usage  ();    // return description of expected opts for create(opts)
IAnalyzer*  create (const char*    opts); // create and return an instance of an Analyzer
void        destroy(IAnalyzer* instance); // destroy created instance of an Analyzer

// These calls implemented by nfstrace
void print_nfs3_procedures(std::ostream& out, const ProcEnum::NFSProcedure proc);
void print_session(std::ostream& out, const Session& session);
void print_nfs_fh3(std::ostream& out, const FH& fh);
}

inline std::ostream& operator<<(std::ostream& out, const Session& session)
{
    print_session(out, session);
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const ProcEnum::NFSProcedure proc)
{
    print_nfs3_procedures(out, proc);
    return out;
}
//------------------------------------------------------------------------------
#endif //PLUGIN_API_H
//------------------------------------------------------------------------------
