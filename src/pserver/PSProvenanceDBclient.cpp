#include<chimbuko/pserver/PSProvenanceDBclient.hpp>
#include<chimbuko/verbose.hpp>
#include<chimbuko/util/string.hpp>

#ifdef ENABLE_PROVDB

using namespace chimbuko;

PSProvenanceDBclient::~PSProvenanceDBclient(){ 
  disconnect();
  VERBOSE(std::cout << "PSProvenanceDBclient exiting" << std::endl);
}

sonata::Collection & PSProvenanceDBclient::getCollection(const GlobalProvenanceDataType type){ 
  switch(type){
  case GlobalProvenanceDataType::FunctionStats:
    return m_coll_funcstats;
  case GlobalProvenanceDataType::CounterStats:
    return m_coll_counterstats;
  default:
    throw std::runtime_error("Invalid type");
  }
}

const sonata::Collection & PSProvenanceDBclient::getCollection(const GlobalProvenanceDataType type) const{ 
  return const_cast<PSProvenanceDBclient*>(this)->getCollection(type);
}



void PSProvenanceDBclient::disconnect(){
  if(m_is_connected){
    VERBOSE(std::cout << "PSProvenanceDBclient disconnecting" << std::endl);

    if(m_perform_handshake){
      VERBOSE(std::cout << "PSProvenanceDBclient de-registering with server" << std::endl);
      m_client_goodbye->on(m_server)();    
      VERBOSE(std::cout << "PSProvenanceDBclient deleting handshake RPCs" << std::endl);

      m_client_hello->deregister();
      m_client_goodbye->deregister();
    
      delete m_client_hello; m_client_hello = nullptr;
      delete m_client_goodbye; m_client_goodbye = nullptr;
    }

    m_is_connected = false;
    VERBOSE(std::cout << "PSProvenanceDBclient disconnected" << std::endl);
  }
}

void PSProvenanceDBclient::connect(const std::string &addr){
  try{    
    //Reset the protocol if necessary
    std::string protocol = ADProvenanceDBengine::getProtocolFromAddress(addr);   
    if(ADProvenanceDBengine::getProtocol().first != protocol){
      int mode = ADProvenanceDBengine::getProtocol().second;
      VERBOSE(std::cout << "PSProvenanceDBclient reinitializing engine with protocol \"" << protocol << "\"" << std::endl);
      ADProvenanceDBengine::finalize();
      ADProvenanceDBengine::setProtocol(protocol,mode);      
    }      

    std::string db_name = "provdb.global";

    thallium::engine &eng = ADProvenanceDBengine::getEngine();
    m_client = sonata::Client(eng);
    VERBOSE(std::cout << "PSProvenanceDBclient connecting to database " << db_name << " on address " << addr << std::endl);
    m_database = m_client.open(addr, 0, db_name);
    VERBOSE(std::cout << "PSProvenanceDBclient opening function stats collection" << std::endl);
    m_coll_funcstats = m_database.open("func_stats");
    VERBOSE(std::cout << "PSProvenanceDBclient opening counter stats collection" << std::endl);
    m_coll_counterstats = m_database.open("counter_stats");
    
    m_server = eng.lookup(addr);

    if(m_perform_handshake){
      VERBOSE(std::cout << "PSProvenanceDBclient registering RPCs and handshaking with provDB" << std::endl);
      m_client_hello = new thallium::remote_procedure(eng.define("pserver_hello").disable_response());
      m_client_goodbye = new thallium::remote_procedure(eng.define("pserver_goodbye").disable_response());
      m_client_hello->on(m_server)();
    }      

    m_is_connected = true;
    VERBOSE(std::cout << "PSProvenanceDBclient connected successfully" << std::endl);
    
  }catch(const sonata::Exception& ex) {
    throw std::runtime_error(std::string("PSProvenanceDBclient could not connect due to exception: ") + ex.what());
  }
}

uint64_t PSProvenanceDBclient::sendData(const nlohmann::json &entry, const GlobalProvenanceDataType type) const{
  if(!entry.is_object()) throw std::runtime_error("JSON entry must be an object");
  if(m_is_connected){
    return getCollection(type).store(entry.dump());
  }
  return -1;
}

std::vector<uint64_t> PSProvenanceDBclient::sendMultipleData(const nlohmann::json &entries, const GlobalProvenanceDataType type) const{
  if(!entries.is_array()) throw std::runtime_error("JSON object must be an array");
  size_t size = entries.size();
  
  if(size == 0 || !m_is_connected) 
    return std::vector<uint64_t>();

  std::vector<uint64_t> ids(size,-1);
  std::vector<std::string> dump(size);
  for(int i=0;i<size;i++){
    if(!entries[i].is_object()) throw std::runtime_error("Array entries must be JSON objects");
    dump[i] = entries[i].dump();
  }

  getCollection(type).store_multi(dump, ids.data()); 
  return ids;
}  


bool PSProvenanceDBclient::retrieveData(nlohmann::json &entry, uint64_t index, const GlobalProvenanceDataType type) const{
  if(m_is_connected){
    std::string data;
    try{
      getCollection(type).fetch(index, &data);
    }catch(const sonata::Exception &e){
      return false;
    }
    entry = nlohmann::json::parse(data);
    return true;
  }
  return false;
}

std::vector<std::string> PSProvenanceDBclient::retrieveAllData(const GlobalProvenanceDataType type) const{
  std::vector<std::string> out;
  if(m_is_connected)
    getCollection(type).all(&out);
  return out;
}

std::vector<std::string> PSProvenanceDBclient::filterData(const GlobalProvenanceDataType type, const std::string &query) const{
  std::vector<std::string> out;
  if(m_is_connected)
    getCollection(type).filter(query, &out);
  return out;
}

    
#endif
  
