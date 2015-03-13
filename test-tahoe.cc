/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/tcp-option.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FifthScriptExample");

// ===========================================================================
//
//         node 0                 node 1
//   +----------------+    +----------------+
//   |  MyApp -sender |    |    PacketSink  |
//   +----------------+    +----------------+
//   |    ns-3 TCP    |    |    ns-3 TCP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                5 Mbps, 2 ms
// This application only send packets as quicly as possible.
class MyApp : public Application
{
public:

  MyApp ();
  virtual ~MyApp();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0),
    m_peer (),
    m_packetSize (0),
    m_nPackets (0),
    m_dataRate (0),
    m_sendEvent (),
    m_running (false),
    m_packetsSent (0)
{
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);

  if (++m_packetsSent < m_nPackets)
    {
      ScheduleTx ();
    }
}

void
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}


typedef enum {
  BEGIN, // 0 set values
  WITHOUT_DROP, //1
  DROPED_TIMEOUT, //2 - packet was droped, and expect dup ack (2 or more)
  DROPED_TRIPLEACK, // 3  - receive 3 or more dup ack
  DROPED, //4 - packet was droped, but  first dupAck dont receive
  LAST_STATE
} TestState;

class TahoeTestCase : public TestCase
{
public:
  TahoeTestCase ();
  TahoeTestCase(double RateOfDrop);
  virtual ~TahoeTestCase ();

  //this three functions are public temporary, for debuging this class
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);
    double RateOfDrop;

private:

  void CwndChange (uint32_t, uint32_t); // tracing changes of cwnd on n0
  void SlowStartThreshold (uint32_t, uint32_t);// tracing changes of ssthreshold on n0
  void RxDrop (Ptr<const Packet> p); // Callback which call, when packet is droped on n1
  void ReceivePack (Ptr<const Packet> pack); // tracing of accepted packets on n1
  void SendPack (Ptr<const Packet> pack);// tracing of sended packets on n1
  void afterChanged(); // call after changed cwnd or ssthreashold
  void ReceivePackOnSender (Ptr<const Packet> pack); // tracing of accepted packets on n1
  void SendPackOnSender (Ptr<const Packet> pack); // tracing of sended packets on n0
  void RTOChange(Time oldRTO, Time newRTO); // tracing of changes of RTO on socket on n0
  void checkTimeout(); // check occure timeout or not
  void CheckChanges(); // check cwnd and ssthreshold after timeout or triple dup
  TcpHeader getTcpHeader(Ptr<const Packet> p); // get TCPheader from packet
  double delayChannel; // time of delay of chanal
  uint32_t sstreshold; // current ssthreashold
  uint32_t cwndOld; // current cwnd
  uint32_t mss; // how determine at run-time?? Now it just 536 (get from experience)
  TestState state; // expected current state of TCPsocket
  uint32_t numberReceivedPack;
  uint32_t numberDupAck;
  ns3::SequenceNumber32 previousAck; // number previous ack
  std::list<ns3::SequenceNumber32> sendedSequnces; // list for which sended packets are confirmed
  Time rto; // current retransmission time out
  bool cwndWasChanged;
  bool ssthdWasChanged;
  bool returnToNormal;

};
TahoeTestCase::TahoeTestCase():TestCase("TahoeTest"){

}
TahoeTestCase::TahoeTestCase(double RateOfDrop):TestCase("TahoeTest"){
    this->RateOfDrop = RateOfDrop;
}

TahoeTestCase::~TahoeTestCase(){

}

TcpHeader TahoeTestCase::getTcpHeader(Ptr<const Packet> pack){
    Packet p = *pack;
    PppHeader ppp;
    p.RemoveHeader (ppp);
    Ipv4Header ip;
    p.RemoveHeader (ip);
    TcpHeader tcp;
    p.PeekHeader (tcp);
    return tcp;
}

void TahoeTestCase::CheckChanges (){
    if (cwndWasChanged == false){
        NS_ASSERT_MSG(cwndOld == mss, "cwnd don't changed on state: "<<state);
    }
    if (ssthdWasChanged == false){
        NS_ASSERT_MSG(sstreshold == mss*2, "ssthreshold don't changed on state: "<<state);
    }
}

void TahoeTestCase ::CwndChange (uint32_t oldCwnd, uint32_t newCwnd)
{
  NS_LOG_UNCOND ("Change cwnd: "<<Simulator::Now ().GetSeconds () << "\t" << newCwnd<< " STATE: "<<state);
  switch (state) {
  case BEGIN:
       NS_LOG_UNCOND ("Change cwnd: begin state");
      cwndOld = oldCwnd;
      break;
  case WITHOUT_DROP:
      if (cwndOld<sstreshold){ // slow start
          //NS_LOG_UNCOND(cwndOld);
          NS_ASSERT_MSG(newCwnd-oldCwnd == mss, "in slow start, cwnd was increased not at rate of mss");
      }
      else if (cwndOld >= sstreshold){ //congestion avoidance
          uint32_t diff = newCwnd-oldCwnd;
          NS_ASSERT_MSG(diff <=mss,"in congestion avoidance, cwnd increase more than mss");
          bool correct = false;
          // this is two method for calculate increasing cwnd in congestion avoidance
          if (diff == 1/(oldCwnd/mss)){
              correct = true;
          }
          if (diff == mss*mss/oldCwnd){
              correct = true;
          }
          NS_ASSERT_MSG(correct, "in congestion avoidance, cwnd uncorrectly increase");
      }
      break;
   case DROPED_TIMEOUT:
        NS_ASSERT_MSG (newCwnd == mss, "cwnd don't reduce to mss after dropped packet");
      break;
   case DROPED_TRIPLEACK:
      NS_ASSERT_MSG (newCwnd == mss, "cwnd don't reduce to mss after dropped packet");
      break;
  default:
      NS_LOG_UNCOND("CwndChange at undetermined TestState: "<< state);
      break;
  }
  cwndOld = newCwnd;
  cwndWasChanged = true;
}

void TahoeTestCase::SlowStartThreshold (uint32_t oldSST, uint32_t newSST)
{    
   NS_LOG_UNCOND ("Change threahold: "<<Simulator::Now ().GetSeconds () << "\t"<<"from "<<oldSST<<" to " << newSST<<
                  " STATE: "<<state);
   switch (state) {
    case BEGIN:
        sstreshold = newSST;
        break;
    case WITHOUT_DROP:
 //        NS_ASSERT_MSG(newSST-oldSST == 0, "without dropping ssthreashold must not increase");
        break;
    case DROPED_TIMEOUT:
        NS_ASSERT_MSG (newSST <= std::max(cwndOld/2,2*mss), "sstheashold don't reduced after timeout");
     case DROPED_TRIPLEACK:
//       When a TCP sender detects segment loss using the retransmission timer
//         and the given segment has not yet been resent by way of the
//         retransmission timer, the value of ssthresh MUST be set to no more
//         than the value given in equation (4):

//            ssthresh = max (FlightSize / 2, 2*SMSS)            (4)

//         where, as discussed above, FlightSize is the amount of outstanding
//         data in the network.
//       Implementation Note: An easy mistake to make is to simply use cwnd,
//       rather than FlightSize, which in some implementations may
//       incidentally increase well beyond rwnd.

       // How determine FlightSize? - is it cwnd?
       // Now (temporary, use only cwnd/2;
          NS_ASSERT_MSG (newSST <= std::max(cwndOld/2,2*mss), "sstheashold don't reduced after triple dup ack");
        break;
    default:
        NS_LOG_UNCOND("ssthreashold Change at undetermined TestState: "<< state);
        break;
    }
   sstreshold = newSST;
   ssthdWasChanged = true;
}

void TahoeTestCase::RxDrop (Ptr<const Packet> p)
{
  NS_LOG_UNCOND ("RxDrop at " << Simulator::Now ().GetSeconds ());
  numberDupAck = 0;
}


void TahoeTestCase::ReceivePack (Ptr<const Packet> pack){
    TcpHeader tcp = getTcpHeader (pack);
    NS_LOG_UNCOND ("Receive data: " << Simulator::Now ().GetSeconds ()<<" seqNum: "
                   <<tcp.GetSequenceNumber ()<< " winSize: "<< tcp.GetWindowSize ()<<
                   " length: "<< tcp.GetLength ());
}

void TahoeTestCase::SendPack (Ptr<const Packet> pack){
    TcpHeader tcp = getTcpHeader (pack);
    NS_LOG_UNCOND ("Send Ack " << Simulator::Now ().GetSeconds ()<<" ack: "<<tcp.GetAckNumber ());
}

void TahoeTestCase::ReceivePackOnSender (Ptr<const Packet> pack){
    TcpHeader tcp = getTcpHeader (pack);
    NS_LOG_UNCOND ("Receive data on Sender: " << Simulator::Now ().GetSeconds ()<<
                   " Ack: "<< tcp.GetAckNumber ());
    if (previousAck == tcp.GetAckNumber ())
        numberDupAck++;
    else {
        numberDupAck = 0;
    }
    if (numberDupAck ==3){
        NS_LOG_UNCOND("state to drop TripleAck");
        state = DROPED_TRIPLEACK;
        cwndWasChanged = false;
        ssthdWasChanged = false;
    }
    previousAck = tcp.GetAckNumber ();
}

void TahoeTestCase::SendPackOnSender (Ptr<const Packet> pack){
    TcpHeader tcp = getTcpHeader (pack);
    sendedSequnces.push_back (tcp.GetSequenceNumber ());
    Simulator::Schedule (rto, &TahoeTestCase::checkTimeout, this);
    CheckChanges();
    state = WITHOUT_DROP;
}

void TahoeTestCase::checkTimeout(){
    if (sendedSequnces.size ()>0){
        NS_LOG_UNCOND(Simulator::Now ().GetSeconds ()<<" check timeout prev: "<<previousAck.GetValue ()<< " exp: "<< sendedSequnces.begin ()->GetValue ());
    if (previousAck.GetValue () <= sendedSequnces.begin ()->GetValue ()){
        NS_LOG_UNCOND("TIMEOUT!!!" <<Simulator::Now ().GetSeconds ());
        state = DROPED_TIMEOUT;
        cwndWasChanged = false;
        ssthdWasChanged = false;
        sendedSequnces.clear ();
    }
    else{
        sendedSequnces.pop_front ();
    }
    }
}

void TahoeTestCase::RTOChange(Time oldRTO, Time newRTO){
    rto = newRTO;
    rto = Seconds(rto.GetSeconds ()-0.00001);
    NS_LOG_UNCOND("Change RTO: from "<<oldRTO << " to "<<newRTO);
}

void TahoeTestCase::DoSetup (){
    std::string tcpModel ("ns3::TcpTahoe");
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (tcpModel));
    mss = 536;
    returnToNormal = false;
}

void TahoeTestCase::DoRun (){
    NodeContainer nodes;
    nodes.Create (2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("100ms"));
    delayChannel = 0.1;

    NetDeviceContainer devices;
    devices = pointToPoint.Install (nodes);

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute ("ErrorRate", DoubleValue (RateOfDrop));
    devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

  //  Ptr<ReceiveListErrorModel> em = CreateObject<ReceiveListErrorModel> ();
  //  std::list<uint32_t> errors;
  //  errors.push_back (2);
  //  errors.push_back (3);
  //  errors.push_back (4);
  //  errors.push_back (6);
  //  em->SetList (errors);
  //  devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

    InternetStackHelper stack;
    stack.Install (nodes);
    Ipv4AddressHelper address;
    address.SetBase ("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer interfaces = address.Assign (devices);

    uint16_t sinkPort = 8080;
    Address sinkAddress (InetSocketAddress (interfaces.GetAddress (1), sinkPort));
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
    ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (1));
    sinkApps.Start (Seconds (0.));
    sinkApps.Stop (Seconds (140.));

    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (nodes.Get (0), TcpSocketFactory::GetTypeId ());
    ns3TcpSocket->TraceConnectWithoutContext ("CongestionWindow", MakeCallback (&TahoeTestCase::CwndChange, this));
    ns3TcpSocket->TraceConnectWithoutContext ("SlowStartThreshold", MakeCallback (&TahoeTestCase::SlowStartThreshold, this));
    ns3TcpSocket->TraceConnectWithoutContext ("RTO", MakeCallback (&TahoeTestCase::RTOChange, this));

    Ptr<MyApp> app = CreateObject<MyApp> ();
    app->Setup (ns3TcpSocket, sinkAddress, 10000, 10, DataRate ("1Mbps"));
    nodes.Get (0)->AddApplication (app);
    app->SetStartTime (Seconds (1.));
    app->SetStopTime (Seconds (140.));

    Ptr<PointToPointNetDevice> ptp1 = DynamicCast<PointToPointNetDevice >(devices.Get (1));
    ptp1->TraceConnectWithoutContext ("MacRx", MakeCallback(&TahoeTestCase::ReceivePack,  this));
    ptp1->TraceConnectWithoutContext ("MacTx", MakeCallback(&TahoeTestCase::SendPack, this));

    Ptr<PointToPointNetDevice> ptp0 = DynamicCast<PointToPointNetDevice >(devices.Get (0));
    ptp0->TraceConnectWithoutContext ("MacRx", MakeCallback(&TahoeTestCase::ReceivePackOnSender,  this));
    ptp0->TraceConnectWithoutContext ("MacTx", MakeCallback(&TahoeTestCase::SendPackOnSender, this));


    devices.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&TahoeTestCase::RxDrop, this));

    Simulator::Stop (Seconds (150));
    Simulator::Run ();
    Simulator::Destroy ();

}

void TahoeTestCase::DoTeardown (){

}

// Now through main-function it was easy to debug my test-case
//class TahoeTestSuit : public TestSuite
//{
//public:
//  TahoeTestSuit ();
//};

//TahoeTestSuit::TahoeTestSuit ()
//  : TestSuite ("ns3-tcp-tahoe", SYSTEM)
//{
//  AddTestCase (new TahoeTestCase(0.0001), TestCase::QUICK);
//  AddTestCase (new TahoeTestCase(0.001), TestCase::QUICK);
//  AddTestCase (new TahoeTestCase(0.01), TestCase::QUICK);
//  AddTestCase (new TahoeTestCase(0.1), TestCase::QUICK);
//}

//static TahoeTestSuit tahoeTestSuit;


int
main (int argc, char *argv[])
{
    NS_LOG_UNCOND("Test 1");
  TahoeTestCase test1;
  test1.RateOfDrop = 0.0001;
  test1.DoSetup ();
  test1.DoRun ();
  test1.DoTeardown ();
NS_LOG_UNCOND("Test 2");
  TahoeTestCase test2;
  test2.RateOfDrop = 0.001;
  test2.DoSetup ();
  test2.DoRun ();
  test2.DoTeardown ();
NS_LOG_UNCOND("Test 3");
  TahoeTestCase test3;
  test3.RateOfDrop = 0.01;
  test3.DoSetup ();
  test3.DoRun ();
  test3.DoTeardown ();
NS_LOG_UNCOND("Test 4");
  TahoeTestCase test4;
  test4.RateOfDrop = 0.1;
  test4.DoSetup ();
  test4.DoRun ();
  test4.DoTeardown ();
}
