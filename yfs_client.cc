// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  //  lc = new lock_client(lock_dst);
  srand((unsigned)time(NULL));
  // unsigned long long N = 4294967296;
  load_root(ec); 
}

void yfs_client::load_root(extent_client *ec) {
  // check if directory 0x00000001 exist
  inum inum = 0x00000001;
  dirinfo din;
  
  // if not exist, create one
  if(getdir(inum, din) != OK) {
    std::string buf;      // empty directory
    ec->put(inum, buf);
    std::cout<<"create root\n";
  }

  // otherwise, do nothing and return 
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

std::vector<yfs_client::dirent> yfs_client::parse_dirents(const std::string &buf) {

  std::vector<dirent> ents;
  if(buf.size() == 0) { return ents; }

  std::istringstream ss(buf);
  std::string str_entry;
  while(std::getline(ss, str_entry, ';')) {
    dirent entry;
    entry.name = str_entry.substr(0, str_entry.find(',') );
    entry.inum = n2i(str_entry.substr( str_entry.find(',')+1));
    ents.push_back(entry);
  }
  return ents;
}

int
yfs_client::set_attr_size(inum file_inum, unsigned int new_size) {
  int r = yfs_client::OK;

  // get old attr and content
  extent_protocol::attr a;
  if(extent_protocol::OK != ec->getattr(file_inum, a)) {
    return IOERR;
  }
  unsigned int old_size = a.size;

  std::string buf;
  if(extent_protocol::OK != ec->get(file_inum, buf)) {
    return IOERR;
  }

  if(new_size >  old_size) {
    // pading at end of buf with '\0'
    buf.resize(new_size, '\0');
  }else if(new_size < old_size) {
    // truncate buf to new size
    buf.resize(new_size);
  }else {
    // do nothing
  }
  
  if(extent_protocol::OK != ec->put(file_inum, buf)) {
    return IOERR;
  } 

  return r;
} 

int
yfs_client::write(inum file_inum, size_t size, off_t offset, const std::string &buf) {
  int r = OK;
 
  // get old data before write 
  std::string old_data;
  if(extent_protocol::OK != ec->get(file_inum, old_data)) {
    return IOERR;
  }

  std::string new_data;  
  if(offset < old_data.size()) {
    // new_data = 2 parts;
    new_data = old_data.substr(0, offset);
    // 
    std::cout<<"Dead here"<<std::endl;
    new_data.append(buf);
    
    // in case offset + size < old_data.size()
    if(offset + size < old_data.size()) {
     std::cout<<"Dead here"<<std::endl;
    
      // append the tail of old data to new data.
      new_data.append(old_data.substr(offset + size));
    }
  }else{ 
// otherwise append null '\0' character to fill the gap, could be zero
	  std::cout<<"Dead here"<<std::endl;
	  // when offset = tmp_buf.size()
	  new_data = old_data;
	  new_data.append(offset-old_data.size(), '\0');
	  new_data.append(buf);
	  std::cout<<"Dead here"<<std::endl;
  }

  if(extent_protocol::OK != ec->put(file_inum, new_data)) {
    return IOERR;
  }

  std::cout<<"buff to write in yfs_client, size is: "<<new_data.size()
           <<" and "<<new_data<<std::endl;
  return r;
}


int
yfs_client::read(inum file_inum, size_t size, off_t offset, std::string &buf) {
  int r = OK;
  
  std::string tmp_buf;
  if(extent_protocol::OK != ec->get(file_inum, tmp_buf)) {
    return IOERR;
  }
  
  // should I consider the logic here? 
  if(offset >= tmp_buf.size()) {
    return OK;
  }

  // fetch requested sub string based on size and offset  
  if(offset + size > tmp_buf.size()) {
    buf = tmp_buf.substr(offset);
  }else{
    buf = tmp_buf.substr(offset, size);
  }
  
  return r;
}

int 
yfs_client::read_dirents(inum directory_inum, std::vector<dirent> &ents) {
  int r = OK;
  std::string buf;
  if(extent_protocol::OK != ec->get(directory_inum, buf)) {
    return IOERR;
  }

  ents = parse_dirents(buf);
  return r;
}

bool
yfs_client::exist(inum parent_inum, const char* name, inum & inum) {

// retrieve content of parent_inum, search if name appears
  std::string buf;
  if(extent_protocol::OK != ec->get(parent_inum, buf)) {
    return false;
  }
  std::vector<dirent> ents = parse_dirents(buf);
   
  for(std::vector<dirent>::iterator it = ents.begin(); it != ents.end(); it++) {
    if(0 == (*it).name.compare(name)) {
      inum = (*it).inum; 
      return true;
    }
  }
     
  return false;
}

int
yfs_client::create(inum parent_inum, const char* name, inum &inum)
{
  int r = OK;

  // Check if a file with the same name already exist
  // under parent directory  
  if(exist(parent_inum, name, inum)) { 
    r = EXIST; 
    // goto release;
    return r;
  }
  
   // Pick up an ino for file name set the 32nd bit to 1
  unsigned long long rnd = rand();
  inum = rnd | 0x80000000; // set the 32nd bit to 1

  // Create an empty extent for ino. 
  std::string empty_str;
  if (ec->put(inum, empty_str) != extent_protocol::OK) {
    r = IOERR;
    return r;
  }

  // Update parent directory's buf in extent server
  std::string buf;
  ec->get(parent_inum, buf);
  buf.append(name);
  buf.append(",");
  buf.append(filename(inum));
  buf.append(";");
  if(ec->put(parent_inum, buf) != extent_protocol::OK) {
    r = IOERR;
    // goto release;
    return r;
  }
  
  return r;
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}



