//------------------------------------------------------------------------------
// Author: Dzianis Huznou
// Description: Different rpc structures.
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
#ifndef RPC_UTILS_H
#define RPC_UTILS_H
//------------------------------------------------------------------------------
#include "protocols/rpc/rpc_header.h"
#include "protocols/xdr/xdr_reader.h"
//------------------------------------------------------------------------------
namespace NST
{
namespace protocols
{
namespace rpc
{

using namespace NST::protocols::xdr;

inline XDRReader& operator>>(XDRReader& in, OpaqueAuth& o)
{
    in >> o.flavor;
    in.read_variable_len(o.body);
    return in;
}

inline XDRReader& operator>>(XDRReader& in, MismatchInfo& o)
{
    return in >> o.low >> o.high;
}

inline XDRReader& operator>>(XDRReader& in, RPCMessage& o)
{
    return in >> o.xid >> o.type;
}

inline XDRReader& operator>>(XDRReader& in, RPCCall& o)
{
    const size_t size = sizeof(o.xid) +
                        sizeof(o.type) +
                        sizeof(o.rpcvers) +
                        sizeof(o.prog) +
                        sizeof(o.vers) +
                        sizeof(o.proc);
    in.arrange_check(size);
    in.read_unchecked(o.xid);   // direct fill RPCMessage fileds
    in.read_unchecked(o.type);  // direct fill RPCMessage fileds
    in.read_unchecked(o.rpcvers);
    in.read_unchecked(o.prog);
    in.read_unchecked(o.vers);
    in.read_unchecked(o.proc);
    return in >> o.cred >> o.verf;
}


inline XDRReader& operator>>(XDRReader& in, AcceptedReply& o)
{
    in >> o.verf >> o.stat;
    switch(o.stat)
    {
        case AcceptStat::SUCCESS:
            // Data will be parsed in the specific reader.
            break;
        case AcceptStat::PROG_MISMATCH:
            in >> o.mismatch_info;
            break;
        case AcceptStat::PROG_UNAVAIL:
        case AcceptStat::PROC_UNAVAIL:
        case AcceptStat::GARBAGE_ARGS:
        case AcceptStat::SYSTEM_ERR:
            break;
    }
    return in;
}

inline XDRReader& operator>>(XDRReader& in, RejectedReply& o)
{
    in >> o.stat;
    switch(o.stat)
    {
        case RejectStat::RPC_MISMATCH:   in >> o.u.mismatch_info; break;
        case RejectStat::AUTH_ERROR:     in >> o.u.auth_stat;     break;
    }
    return in;
}

inline XDRReader& operator>>(XDRReader& in, RPCReply& o)
{
    const size_t size = sizeof(o.xid) +
                        sizeof(o.type) +
                        sizeof(o.stat);
    in.arrange_check(size);
    in.read_unchecked(o.xid);   // direct fill RPCMessage fileds
    in.read_unchecked(o.type);  // direct fill RPCMessage fileds
    in.read_unchecked(o.stat);
    switch(o.stat)
    {
        case ReplyStat::MSG_ACCEPTED:  in >> o.u.accepted; break;
        case ReplyStat::MSG_DENIED:    in >> o.u.rejected; break;
    }
    return in;
}

} // namespace rpc
} // namespace protocols
} // namespace NST
//------------------------------------------------------------------------------
#endif//RPC_UTILS_H
//------------------------------------------------------------------------------
