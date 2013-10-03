//------------------------------------------------------------------------------
// Author: Pavel Karneliuk
// Description: Generic processor for filtration raw pcap packets.
// Copyright (c) 2013 EPAM Systems. All Rights Reserved.
// TODO: THIS CODE MUST BE TOTALLY REFACTORED!
//------------------------------------------------------------------------------
#ifndef FILTRATION_PROCESSOR_H
#define FILTRATION_PROCESSOR_H
//------------------------------------------------------------------------------
#include <cassert>
#include <algorithm>
#include <string>

#include <tr1/unordered_map>

#include <pcap/pcap.h>

#include "../../auxiliary/exception.h"
#include "../../auxiliary/logger.h"
#include "../../auxiliary/session.h"
#include "../../auxiliary/unique_ptr.h"
#include "../../controller/parameters.h"
#include "../packet_info.h"
#include "../packet.h"
#include "../rpc/rpc_header.h"
//------------------------------------------------------------------------------
using NST::auxiliary::Exception;
using NST::auxiliary::Logger;
using NST::auxiliary::Session;
using NST::auxiliary::UniquePtr;

using namespace NST::filter::rpc;
//------------------------------------------------------------------------------
namespace NST
{
namespace filter
{

// Represents TCP conversation between node A and node B
template <typename StreamReader>
struct TCPSession
{
public:

    struct Flow
    {
        // Helpers for comparison sequence numbers
        // Idea for gt: either x > y, or y is much bigger (assume wrap)
        inline static bool GT_SEQ(uint32_t x, uint32_t y){ return (int32_t)((y) - (x)) <  0; }
        inline static bool LT_SEQ(uint32_t x, uint32_t y){ return (int32_t)((x) - (y)) <  0; }
        inline static bool GE_SEQ(uint32_t x, uint32_t y){ return (int32_t)((y) - (x)) <= 0; }
        inline static bool LE_SEQ(uint32_t x, uint32_t y){ return (int32_t)((x) - (y)) <= 0; }
        inline static bool EQ_SEQ(uint32_t x, uint32_t y){ return           (x) ==(y);       }

        friend class TCPSession<StreamReader>;

        Flow() : fragments(NULL), sequence(0)
        {
        }
        ~Flow()
        {
            reset();
        }

//        Flow(const Flow&);            // undefiend
//        Flow& operator=(const Flow&); // undefiend

        void reset()
        {
            reader.reset(); // reset state of Reader
            while(fragments)
            {
                Packet* c = fragments;
                fragments = c->next;
                Packet::destroy(c);
            }

            sequence = 0;
        }

        void reassemble(PacketInfo& info)
        {
            uint32_t seq = info.tcp->seq();
            uint32_t len = info.dlen;

            if( sequence == 0 ) // this is the first time we have seen this src's sequence number
            {
                sequence = seq + len;
                if( info.tcp->is(tcp_header::SYN) )
                {
                    sequence++;
                }

                if(len > 0)
                {
                    reader.push(info);  // write out the packet data
                }

                return;
            }

            // if we are here, we have already seen this src, let's
            // try and figure out if this packet is in the right place
            if( seq < sequence )
            {
                // this sequence number seems dated, but
                // check the end to make sure it has no more
                // info than we have already seen
                uint32_t newseq = seq + len;
                if( newseq > sequence )
                {

                    // this one has more than we have seen. let's get the
                    // payload that we have not seen
                    uint32_t new_len = sequence - seq;

                    if ( info.dlen <= new_len )
                    {
                        info.data = NULL;
                        info.dlen = 0;
                    }
                    else
                    {
                        assert(info.dlen >= new_len);
                        info.data += new_len;
                        info.dlen -= new_len;
                    }

                    seq = sequence;
                    len = newseq - sequence;

                    // this will now appear to be right on time :)
                }
            }

            if ( seq == sequence ) // right on time
            {
                sequence += len;
                if( info.tcp->is(tcp_header::SYN) ) sequence++;

                if( info.data && info.dlen > 0)
                {
                    reader.push(info);
                }
                // done with the packet, see if it caused a fragment to fit
                while( check_fragments(0) );
            }
            else // out of order packet
            {
                if(info.dlen > 0 && (seq > sequence) )
                {
                    //TRACE("ADD FRAGMENT seq: %u dlen: %u sequence: %u", seq, info.dlen, sequence);
                    Packet* frag = Packet::create(info);

                    frag->next = fragments;
                    fragments = frag;
                }
            }
        }

        bool check_fragments(const uint32_t acknowledged)
        {
            Packet* current = fragments;
            if( current )
            {
                Packet* prev = NULL;
                uint32_t lowest_seq = current->tcp->seq();
                while( current )
                {
                    const uint32_t current_seq = current->tcp->seq();
                    const uint32_t current_len = current->dlen;

                    if( GT_SEQ(lowest_seq, current_seq) ) // lowest_seq > current_seq
                    {
                        lowest_seq = current_seq;
                    }

                    if( current_seq < sequence )
                    {
                        bool has_data = false;
                        // this sequence number seems dated, but
                        // check the end to make sure it has no more
                        // info than we have already seen
                        uint32_t newseq = current_seq + current_len;
                        if( newseq > sequence )
                        {
                            // this one has more than we have seen. let's get the
                            // payload that we have not seen. This happens when
                            // part of this frame has been retransmitted
                            uint32_t new_pos = sequence - current_seq;

                            sequence += (current_len - new_pos);

                            if ( current->dlen > new_pos )
                            {
                                has_data = true;
                                current->data += new_pos;
                                current->dlen -= new_pos;
                            }

                        }

                        // Remove the fragment from the list as the "new" part of it
                        // has been processed or its data has been seen already in 
                        // another packet.
                        if( prev )
                        {
                          prev->next = current->next;
                        } else
                        {
                          fragments = current->next;
                        }

                        if(has_data)
                        {
                            TRACE("accepted payload new seq:%u len:%u", sequence, current_len);
                            reader.push(*current);
                        }
                        else
                        {
                            TRACE("drop part of stream seq:%u len:%u", current_seq, current_len);
                        }

                        Packet::destroy(current);

                        return true;
                    }

                    if( current_seq == sequence )
                    {
                        // this fragment fits the stream
                        sequence += current_len;
                        if( prev )
                        {
                            prev->next = current->next;
                        }
                        else
                        {
                            fragments = current->next;
                        }

                        reader.push(*current);
                        Packet::destroy(current);

                        return true;
                    }
                    prev = current;
                    current = current->next;
                }// end while

                if( GT_SEQ(acknowledged, lowest_seq) )  // acknowledged > lowest_seq
                {
                    //TRACE("acknowledged(%u) > lowest_seq(%u) seq:%u", acknowledged, lowest_seq, sequence);
                    // There are frames missing in the capture stream that were seen
                    // by the receiving host. Inform stream about it.
                    reader.lost(lowest_seq - sequence);
                    sequence = lowest_seq;
                    return true;
                }
            }
            return false;
        }

    private:
        StreamReader    reader;     // reader of acknowledged data stream
        Packet*         fragments;  // list of not yet acked fragments
        uint32_t        sequence;
    };

    TCPSession()
    {
    }
    ~TCPSession()
    {
    }

//    TCPSession(const TCPSession&);            // undefiend
//    TCPSession& operator=(const TCPSession&); // undefiend

    template <typename Writer>
    void init(Writer* writer)
    {
        for(uint32_t i=0; i<2; i++)
        {
            flows[i].reader.set_writer(writer);
        }
    }

    void collect(PacketInfo& info, Session::Direction d)
    {
        const uint32_t ack = info.tcp->ack();

        //check whether this frame acks fragments that were already seen.
        while( flows[1-d].check_fragments(ack) );

        flows[d].reassemble(info);
    }

    Flow flows[2];
};

template<typename SessionCollector>
struct IPv4TCPMapper
{
    typedef SessionCollector Collector;

    static inline Session::Direction fill_session(const PacketInfo& info, Session& s)
    {
        s.ip_type = Session::v4;
        s.ip.v4.addr[0] = info.ipv4->src();
        s.ip.v4.addr[1] = info.ipv4->dst();

        s.type = Session::TCP;
        s.port[0] = info.tcp->sport();
        s.port[1] = info.tcp->dport();

        if(s.ip.v4.addr[0] < s.ip.v4.addr[1]) return Session::Source;
        else
        if(s.ip.v4.addr[0] > s.ip.v4.addr[1]) return Session::Destination;
        else // Ok, addresses are equal, compare ports
        return (s.port[0] < s.port[1]) ? Session::Source : Session::Destination;
    }

    struct Hash
    {
        std::size_t operator() (const Session& s) const
        {
            return s.port[0] + s.port[1] + s.ip.v4.addr[0] + s.ip.v4.addr[1];
        }
    };

    struct Pred
    {
        bool operator() (const Session& a, const Session& b) const
        {
            if((a.port[0] == b.port[0]) &&
               (a.port[1] == b.port[1]) &&
               (a.ip.v4.addr[0] == b.ip.v4.addr[0]) &&
               (a.ip.v4.addr[1] == b.ip.v4.addr[1]))
                return true;

            if((a.port[1] == b.port[0]) &&
               (a.port[0] == b.port[1]) &&
               (a.ip.v4.addr[1] == b.ip.v4.addr[0]) &&
               (a.ip.v4.addr[0] == b.ip.v4.addr[1]))
                return true;
            return false;
        }
    };
};


template<typename SessionCollector>
struct IPv4UDPMapper
{
    typedef SessionCollector Collector;

    static inline Session::Direction fill_session(const PacketInfo& info, Session& s)
    {
        s.ip_type = Session::v4;
        s.ip.v4.addr[0] = info.ipv4->src();
        s.ip.v4.addr[1] = info.ipv4->dst();

        s.type = Session::UDP;
        s.port[0] = info.udp->sport();
        s.port[1] = info.udp->dport();

        if(s.ip.v4.addr[0] < s.ip.v4.addr[1]) return Session::Source;
        else
        if(s.ip.v4.addr[0] > s.ip.v4.addr[1]) return Session::Destination;
        else // Ok, addresses are equal, compare ports
        return (s.port[0] < s.port[1]) ? Session::Source : Session::Destination;
    }

    struct Hash
    {
        std::size_t operator() (const Session& s) const
        {
            return s.port[0] + s.port[1] + s.ip.v4.addr[0] + s.ip.v4.addr[1];
        }
    };

    struct Pred
    {
        bool operator() (const Session& a, const Session& b) const
        {
            if((a.port[0] == b.port[0]) &&
               (a.port[1] == b.port[1]) &&
               (a.ip.v4.addr[0] == b.ip.v4.addr[0]) &&
               (a.ip.v4.addr[1] == b.ip.v4.addr[1]))
                return true;

            if((a.port[1] == b.port[0]) &&
               (a.port[0] == b.port[1]) &&
               (a.ip.v4.addr[1] == b.ip.v4.addr[0]) &&
               (a.ip.v4.addr[0] == b.ip.v4.addr[1]))
                return true;
            return false;
        }
    };
};

// Represents UDP datagrams interchange between node A and node B
template <typename Writer>
struct UDPSession
{
public:
    UDPSession()
    {
        max_hdr = controller::Parameters::instance()->rpcmsg_limit();
    }
    ~UDPSession()
    {
    }

//    UDPSession(const UDPSession&);            // undefiend
//    UDPSession& operator=(const UDPSession&); // undefiend

    void init(Writer* w)
    {
        writer = w;
    }

    void collect(PacketInfo& info, Session::Direction d)
    {
        // TODO: this code must be generalized with RPCFiltrator class
    
        uint32_t hdr_len = 0;
        const MessageHeader*const msg = reinterpret_cast<const MessageHeader*>(info.data);
        switch(msg->type())
        {
            case SUNRPC_CALL:
            {
                const CallHeader*const call = static_cast<const CallHeader*const>(msg);
                if(RPCValidator::check(call) && NFSv3Validator::check(call))
                {
                    hdr_len = std::min(info.dlen, max_hdr);
                }
                else
                {
                    return;
                }
            }
            break;
            case SUNRPC_REPLY:
            {
                const ReplyHeader*const reply = static_cast<const ReplyHeader*const>(msg);
                if(RPCValidator::check(reply))
                {
                    hdr_len = std::min(info.dlen, max_hdr);
                }
                else // isn't RPC reply, stream is corrupt
                {
                    return;
                }
            }
            break;
            default:
                return;
        }

        typename Writer::Collection collection;
        collection = *writer;

        collection.push(info, hdr_len);

        collection.complete(info);
    }

    Writer* writer;
    uint32_t max_hdr;
};

template<typename Mapper>
class SessionCollectors
{

public:

    typedef std::tr1::unordered_map<Session, typename Mapper::Collector, typename Mapper::Hash, typename Mapper::Pred> Container;
    typedef typename Container::iterator it;

    SessionCollectors()
    {
    }
    ~SessionCollectors()
    {
    }

    template <typename Writer>
    void collect_packet(PacketInfo& info, Writer* writer)
    {
        Session key;
        const Session::Direction direction = Mapper::fill_session(info, key);

        it i = container.find(key);
        if(i == container.end())
        {
            std::pair<it, bool> res = container.insert(typename Container::value_type(key, typename Mapper::Collector()));
            if(res.second)
            {
                i = res.first;
                i->second.init(writer);
                Logger::Buffer buffer;
                buffer << "create new session " << key;
            }
            else
            {
                Logger::Buffer buffer;
                buffer << "session " << key << " is not created";
                return;
            }
        }

        i->second.collect(info, direction);
    }

private:
    Container container;
};


/*
    Stateful reader of Sun RPC messages 
    Reads data from PacketInfo passed via push() method
    aggregates length of current RPC message and length of RPC message useful for analysis
    TODO: add matching Calls and replies by XID of message
*/
template<typename Writer>
class RPCFiltrator
{
public:
    RPCFiltrator() : writer(NULL)
    {
        max_hdr = controller::Parameters::instance()->rpcmsg_limit();
        reset();
    }

//    RPCFiltrator(const RPCFiltrator&);            // undefiend
//    RPCFiltrator& operator=(const RPCFiltrator&); // undefiend

    inline void reset()
    {
        msg_len = 0;
        hdr_len = 0;
        collection.reset();     // skip collected data
    }

    inline void set_writer(Writer* w)
    {
        assert(w);
        writer = w;
    }

    inline void lost(const uint32_t n) // we are lost n bytes in sequence
    {
        if(msg_len != 0)
        {
            if(hdr_len == 0 && msg_len >= n)
            {
                //TRACE("We are lost %u bytes of payload marked for discard", n);
                msg_len -= n;
            }
            else
            {
                LOG("We are lost %u bytes of useful data. lost:%u msg_len:%u", n - msg_len, n, msg_len);
                reset();
            }
        }
        else
        {
            TRACE("We are lost %u bytes of unknown payload", n);
        }
    }

    void push(PacketInfo& info)
    {
        assert(info.dlen != 0);

        while(info.dlen) // loop over data in packet
        {
            if(msg_len != 0)    // we are on-stream and we are looking to some message
            {
                if(hdr_len == 0)    // message header is readout, discard the unused tail of message
                {
                    if(msg_len >= info.dlen) // discard whole new packet
                    {
                        //TRACE("discard whole new packet");
                        msg_len -= info.dlen;
                        info.dlen = 0;  // return from while
                    }
                    else  // discard only a part of packet payload related to current message
                    {
                        //TRACE("discard only a part of packet payload related to current message");
                        info.dlen -= msg_len;
                        info.data += msg_len;
                        msg_len = 0;
                    }
                }
                else // hdr_len != 0, readout a part of header of current message
                {
                    if(hdr_len > info.dlen) // got new part of header (not the all!)
                    {
                        //TRACE("got new part of header (not the all!)");
                        collection.push(info, info.dlen);
                        hdr_len     -= info.dlen;
                        msg_len     -= info.dlen;
                        info.dlen = 0;  // return from while
                    }
                    else // hdr_len <= dlen, current message will be complete, also we have some additional data
                    {
                        //TRACE("current message will be complete, also we have some additional data");
                        collection.push(info, hdr_len);
                        info.dlen   -= hdr_len;
                        info.data   += hdr_len;

                        msg_len -= hdr_len;
                        hdr_len -= hdr_len; // set 0

                        // we should remove RM(uin32_t) from collected data
                        collection.skip_first(sizeof(RecordMark));

                        collection.complete(info);    // push complete message to queue
                    }
                }
            }
            else // msg_len == 0, no one mesasge is on reading, try to find next message
            {
                find_message(info);
            }
        }
    }

    inline void find_message(PacketInfo& info)
    {
        static const size_t max_header = sizeof(RecordMark) + sizeof(CallHeader);
        const RecordMark* rm;

        if(collection) // collection is allocated
        {
            const uint32_t tocopy = max_header-collection.size();

            if(info.dlen < tocopy)
            {
                collection.push(info, info.dlen);
                //info.data += info.dlen;   optimization
                info.dlen = 0;
                return;
            }
            else // info.dlen >= tocopy
            {
                collection.push(info, tocopy);
                info.dlen -= tocopy;
                info.data += tocopy;

                assert(max_header == collection.size());

                rm = reinterpret_cast<const RecordMark*>(collection.data());
            }
        }
        else // collection is empty
        {
            collection = *writer;   // allocate new collection from writer

            if(info.dlen >= max_header)  // is data enougth to message validation?
            {
                rm = reinterpret_cast<const RecordMark*>(info.data);
            }
            else // push them into collection to validation after supplement by next data
            {
                collection.push(info, info.dlen);
                //info.data += info.dlen;   optimization
                info.dlen = 0;
                return;
            }
        }

        assert(collection);     // collection must be initialized
        assert(rm != NULL);     // RM must be initialized
        assert(msg_len == 0);   // RPC Message still undetected

        //if(rm->is_last()); // TODO: handle sequence field of record mark
        if(validate_header(rm->fragment(), rm->fragment_len() + sizeof(RecordMark) ) )
        {
            assert(msg_len != 0);   // message is found

            const uint32_t written = collection.size();
            if(written != 0) // a message was partially written to collection
            {
                msg_len -= written;
                if(hdr_len != 0) // we want to collect header of this RPC message
                {
                    hdr_len -= written;
                }
            }
        }
        else    // unknown data in packet payload
        {
            assert(msg_len == 0);   // message is not found
            assert(hdr_len == 0);   // header should be skipped
            collection.reset();     // skip collected data
            // skip data od current packet at all
            //info.data = NULL; optimization
            info.dlen = 0;
        }
    }

    inline bool validate_header(const MessageHeader*const msg, const uint32_t len)
    {
        switch(msg->type())
        {
            case SUNRPC_CALL:
            {
                const CallHeader*const call = static_cast<const CallHeader*const>(msg);
                if(RPCValidator::check(call))
                {
                    msg_len = len;   // length of current RPC message

                    if(NFSv3Validator::check(call))
                    {
                        hdr_len = std::min(msg_len, max_hdr);
                        //TRACE("MATCH RPC Call xid:%u len: %u procedure: %u", call->xid(), msg_len, call->proc());
                    }
                    else
                    {
                        hdr_len = 0; // don't collect headers of unknown calls
                        //TRACE("Unknown RPC call of program: %u version: %u procedure: %u", call->prog(), call->vers(), call->proc());
                    }
                    return true;
                }
                else
                {
                    return false;   // isn't RPC Call, stream is corrupt
                }
            }
            break;
            case SUNRPC_REPLY:
            {
                const ReplyHeader*const reply = static_cast<const ReplyHeader*const>(msg);
                if(RPCValidator::check(reply))
                {
                    msg_len = len;   // length of current RPC message
                    hdr_len = std::min(msg_len, max_hdr);
                    //TRACE("MATCH RPC Reply xid:%u len: %u", reply->xid(), msg_len);
                    return true;
                }
                else // isn't RPC reply, stream is corrupt
                {
                    msg_len = 0;
                    hdr_len = 0;
                    return false;
                }
            }
            break;
            default:
            {
                //isn't RPC message
            }
            break;
        }

        return false;
    }

private:
    uint32_t    max_hdr;  // max length of RPC message that will be collected
    uint32_t    msg_len;  // length of current RPC message + RM
    uint32_t    hdr_len;  // min(max_hdr, msg_len) or 0 in case of unknown msg

    Writer*     writer;

    typename Writer::Collection collection;    // storage for collection packet data
};

template
<
    typename Reader,
    typename Writer
>
class FiltrationProcessor
{
public:

    FiltrationProcessor(UniquePtr<Reader>& r, UniquePtr<Writer>& w) : reader(r), writer(w)
    {
        // check datalink layer
        datalink = reader->datalink();
        if(datalink != DLT_EN10MB)
        {
            throw Exception(std::string("Unsupported Data Link Layer: ") + Reader::datalink_description(datalink));
        }
    }
    ~FiltrationProcessor()
    {
        Logger::Buffer buffer;
        reader->print_statistic(buffer);
    }

    void run()
    {
        bool done = reader->loop(this, callback);
        if(done)
        {
            throw Exception("Filtration is done");
        }
    }

    void stop()
    {
        reader->break_loop();
    }

    static void callback(u_char *user, const struct pcap_pkthdr *pkthdr, const u_char* packet)
    {
        FiltrationProcessor* processor = (FiltrationProcessor*) user;

        PacketInfo info(pkthdr, packet, processor->datalink);

        if(info.eth && info.ipv4)
        {
            if(info.tcp)     // Ethernet:IPv4:TCP
            {
                if(pkthdr->caplen == pkthdr->len)
                {
                    return processor->tcp_sessions.collect_packet(info, processor->writer.get());
                }
                else
                {
                    // the pcap packet was truncated by snaplen option
                    // this packed won't correclty reassembled to TCP stream
                    return;
                }
            }
            else if(info.udp)// Ethernet:IPv4:UDP
            {
                return processor->udp_sessions.collect_packet(info, processor->writer.get());
            }
        }
        else
        {
            LOG("only following stack of protocol is supported: Ethernet IPv4 TCP | UDP");
        }
    }

private:

    UniquePtr<Reader> reader;
    UniquePtr<Writer> writer;
    int datalink;
    SessionCollectors< IPv4TCPMapper < TCPSession < RPCFiltrator < Writer > > > > tcp_sessions;
    SessionCollectors< IPv4UDPMapper < UDPSession < Writer > > >                  udp_sessions;
};


} // namespace filter
} // namespace NST
//------------------------------------------------------------------------------
#endif//FILTRATION_PROCESSOR_H
//------------------------------------------------------------------------------
