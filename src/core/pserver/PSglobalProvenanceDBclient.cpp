#include<chimbuko/core/pserver/PSglobalProvenanceDBclient.hpp>
#include<chimbuko/core/verbose.hpp>
#include<chimbuko/core/util/string.hpp>

#ifdef ENABLE_PROVDB

using namespace chimbuko;

PSglobalProvenanceDBclient::~PSglobalProvenanceDBclient(){ disconnect(); } //call disconnect in derived class to ensure derived class is still alive when disconnect is called

void PSglobalProvenanceDBclient::handshakeHello(thallium::engine &eng, thallium::endpoint &server){
  verboseStream << "PSglobalProvenanceDBclient registering RPCs and handshaking with provDB" << std::endl;
  m_client_hello = new thallium::remote_procedure(eng.define("pserver_hello").disable_response());
  m_client_goodbye = new thallium::remote_procedure(eng.define("pserver_goodbye").disable_response());
  m_client_hello->on(server)();
}

void PSglobalProvenanceDBclient::handshakeGoodbye(thallium::engine &eng, thallium::endpoint &server){
  verboseStream << "PSglobalProvenanceDBclient de-registering with server" << std::endl;
  m_client_goodbye->on(server)();    
  verboseStream << "PSglobalProvenanceDBclient deleting handshake RPCs" << std::endl;
  
  m_client_hello->deregister();
  m_client_goodbye->deregister();
  
  delete m_client_hello; m_client_hello = nullptr;
  delete m_client_goodbye; m_client_goodbye = nullptr;
}

void PSglobalProvenanceDBclient::connectServer(const std::string &addr, const int provider_idx){
  connect(addr,ProvDBsetup::getGlobalDBname(),provider_idx);
}

void PSglobalProvenanceDBclient::connectMultiServer(const std::string &addr_file_dir){
  int instance = ProvDBsetup::getGlobalDBinstance(); //which server instance holds the global database?
  int provider = ProvDBsetup::getGlobalDBproviderIndex();
  std::string addr = ProvDBsetup::getInstanceAddress(instance, addr_file_dir);
  connectServer(addr, provider);
}
    
#endif
  
