#include <gtest/gtest.h>
#include <cassert>
#include <chimbuko/ad/ADProvenanceDBclient.hpp>

using namespace chimbuko;

//For these tests the provenance DB admin must be running
std::string addr;

TEST(ADProvenanceDBclientTest, Connects){

  bool connect_fail = false;
  ADProvenanceDBclient client;
  std::cout << "Client attempting connection" << std::endl;
  try{
    client.connect(addr);
  }catch(const std::exception &ex){
    connect_fail = true;
  }
  EXPECT_EQ(connect_fail, false);
}

TEST(ADProvenanceDBclientTest, ConnectsTwice){

  bool connect_fail = false;
  ADProvenanceDBclient client;
  std::cout << "Client attempting connection for second time" << std::endl;
  try{
    client.connect(addr);
  }catch(const std::exception &ex){
    connect_fail = true;
  }
  EXPECT_EQ(connect_fail, false);
}

TEST(ADProvenanceDBclientTest, SendReceiveAnomalyData){

  bool fail = false;
  ADProvenanceDBclient client;
  std::cout << "Client attempting connection" << std::endl;
  try{
    client.connect(addr);

    nlohmann::json obj;
    obj["hello"] = "world";
    std::cout << "Sending " << obj.dump() << std::endl;
    uint64_t rid = client.sendAnomalyData(obj);
    EXPECT_NE(rid, -1);
    
    nlohmann::json check;
    EXPECT_EQ( client.retrieveAnomalyData(check, rid), true );
    
    std::cout << "Testing retrieved anomaly data:" << check.dump() << std::endl;

    //NB, retrieval adds extra __id field, so objects not identical
    bool same = check["hello"] == "world";
    std::cout << "JSON objects are the same? " << same << std::endl;

    EXPECT_EQ(same, true);
  }catch(const std::exception &ex){
    fail = true;
  }
  EXPECT_EQ(fail, false);
}


TEST(ADProvenanceDBclientTest, SendReceiveMetadata){

  bool fail = false;
  ADProvenanceDBclient client;
  std::cout << "Client attempting connection" << std::endl;
  try{
    client.connect(addr);

    nlohmann::json obj;
    obj["hello"] = "world";
    std::cout << "Sending " << obj.dump() << std::endl;
    uint64_t rid = client.sendMetadata(obj);
    EXPECT_NE(rid, -1);
    
    nlohmann::json check;
    EXPECT_EQ( client.retrieveMetadata(check, rid), true );
    
    std::cout << "Testing retrieved metadata:" << check.dump() << std::endl;

    //NB, retrieval adds extra __id field, so objects not identical
    bool same = check["hello"] == "world";
    std::cout << "JSON objects are the same? " << same << std::endl;

    EXPECT_EQ(same, true);
  }catch(const std::exception &ex){
    fail = true;
  }
  EXPECT_EQ(fail, false);
}




int main(int argc, char** argv) 
{
  assert(argc == 2);
  addr = argv[1];
  std::cout << "Provenance DB admin is on address: " << addr << std::endl;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
