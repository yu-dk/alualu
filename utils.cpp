#include "utils.h"

string read_config(string config_file, string key){
  string _key, _value;
  ifstream fin( config_file.c_str());
  if (!fin) 
    try {
      throw 1;    
    } catch(int e) {
      cerr << "#### ERROR #### file: "<< config_file << " not exists!!!" << endl;
    }   
  while (fin >> _key >> _value) {
    if (_key == key) 
      return _value;
  }
  fin.close();  
  try {
    throw 1;    
  } catch(int e) {
    cerr << "#### ERROR #### key: " << key <<" doesn't exist in file:" << config_file << endl;
    exit(0);
  }     
}

void parse_cigar(string cigar, list <char> & opts, list <int> & cnts){
  string cnt;
  int cnt_int;
  opts.clear();
  cnts.clear();
  for (size_t i = 0; i < cigar.size(); i++) {
    if ( !isdigit(cigar[i]) ) {
      opts.push_back(cigar[i]);
      if (!cnt.empty()) {
	seqan::lexicalCast2(cnt_int, cnt);
	cnts.push_back(cnt_int);
      }
      cnt = "";
    } else {
      cnt += cigar[i];
    }
  }
  seqan::lexicalCast2(cnt_int, cnt);
  cnts.push_back(cnt_int);  
}

string get_cigar(seqan::BamAlignmentRecord &record) {
  stringstream ss;
  for (unsigned li = 0; li < length(record.cigar); li++) 
    ss << record.cigar[li].operation << record.cigar[li].count;
  return ss.str();
}

void print_cigar(seqan::BamAlignmentRecord &record,  ostream & os) {
  for (unsigned li = 0; li < length(record.cigar); li++) os << record.cigar[li].operation << record.cigar[li].count; 
}

void print_read(seqan::BamAlignmentRecord &record, ostream & os) {
  os << record.qName << " " << record.beginPos << " " << record.beginPos + getAlignmentLengthInRef(record)  << " " ;
  print_cigar(record, os);
  os << " " << record.pNext << " " << record.beginPos + record.tLen << endl;
}

void get_rID_chrn(string & bam_input, vector<string> &chrns, map<int, seqan::CharString> &rID_chrn){
  seqan::Stream<seqan::Bgzf> inStream;
  assert (open(inStream, bam_input.c_str(), "r"));
  TNameStore      nameStore;
  TNameStoreCache nameStoreCache(nameStore);
  TBamIOContext   context(nameStore, nameStoreCache);
  seqan::BamHeader header;
  seqan::BamAlignmentRecord record;
  assert(!readRecord(header, context, inStream, seqan::Bam()) );
  int rID;
  for ( vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci++) {
    if ( ! getIdByName(nameStore, *ci, rID, nameStoreCache)) 
      if ( ! getIdByName(nameStore, (*ci).substr(3), rID, nameStoreCache)) {
	cerr << "ERROR: Reference sequence named "<< *ci << " not known.\n";
	exit(0);
      }
    rID_chrn[rID] = *ci;
  }  
  seqan::close(inStream);
}

char mappingType(seqan::BamAlignmentRecord &record){
  seqan::BamTagsDict tagsDict(record.tags);
  unsigned myIdx = 0;
  char valChar = 0;  
  if ( findTagKey(myIdx, tagsDict, "X0") and extractTagValue(valChar, tagsDict, myIdx)) return valChar;
  return 0;
}

int numOfBestHits(seqan::BamAlignmentRecord &record){
  seqan::BamTagsDict tagsDict(record.tags);
  unsigned myIdx = 0, valInt = 1;  
  if ( findTagKey(myIdx, tagsDict, "X0")) extractTagValue(valInt, tagsDict, myIdx);
  return max((int)valInt, 1);
}

void read_fasta_by_name( seqan::Dna5String &fa_seq, string file_fa, seqan::CharString fa_name){
  seqan::FaiIndex fai_seq;
  assert( !access( file_fa.c_str(), 0 ) );
  if (read(fai_seq, seqan::toCString( file_fa))) {
    build(fai_seq, seqan::toCString( file_fa));
    seqan::CharString file_fai =  file_fa;
    file_fai += ".fai";
    write(fai_seq, toCString(file_fai));
  }
  unsigned idx = 0;  
  assert (getIdByName(fai_seq, fa_name, idx));
  readSequence(fa_seq, fai_seq, idx);
}

seqan::CharString fasta_seq(string fa_input, string chrn, int beginPos, int endPos, bool upper){
  seqan::FaiIndex faiIndex;
  assert (!read(faiIndex, fa_input.c_str()) );
  unsigned idx = 0;
  assert (getIdByName(faiIndex, chrn, idx));
  seqan::CharString seq;
  assert (!readRegion(seq, faiIndex, idx, beginPos, endPos));
  if (upper) seqan::toUpper(seq);
  return seq;
//    seqan::ModifiedString< seqan::CharString, seqan::ModView<MyUpper> > SEQ(seq);
//    return SEQ;
}

seqan::CharString fasta_seq(seqan::FaiIndex &faiIndex, unsigned idx, int beginPos, int endPos, bool upper){
  seqan::CharString seq;
  assert (!readRegion(seq, faiIndex, idx, beginPos, endPos));
  if (upper) seqan::toUpper(seq);
  return seq;
}

bool find_read(string &bam_input, string &bai_input, string &chrn, string &this_qName, int this_pos, seqan::BamAlignmentRecord &that_record, int flank_region) { // flank_region = 0, print this read; otherwise, print its pair
  int that_begin = this_pos - max(flank_region, 10); 
  int that_end = this_pos + max(flank_region, 10);
  TNameStore      nameStore;
  TNameStoreCache nameStoreCache(nameStore);
  TBamIOContext   context(nameStore, nameStoreCache);
  seqan::BamHeader header;
  seqan::BamAlignmentRecord record;  
  seqan::Stream<seqan::Bgzf> inStream;  
  open(inStream, bam_input.c_str(), "r");
  seqan::BamIndex<seqan::Bai> baiIndex;
  assert( !read(baiIndex, bai_input.c_str())) ;
  int rID;
  assert ( !readRecord(header, context, inStream, seqan::Bam()) ); 
  assert ( getIdByName(nameStore, chrn, rID, nameStoreCache) ); // change context ??
  bool hasAlignments = false;
  jumpToRegion(inStream, hasAlignments, context, rID, that_begin, that_end, baiIndex);
  if (!hasAlignments) return false;
  while (!atEnd(inStream)) {
    assert (!readRecord(record, context, inStream, seqan::Bam())); 
    if (record.rID != rID || record.beginPos >= that_end) break;
    if (record.beginPos < that_begin) continue;
    if (record.qName == this_qName) {
      if ((flank_region > 0 and record.beginPos != this_pos) or (flank_region == 0) ) {
	//cerr << flank_region << " " <<  record.beginPos << " " << this_pos << endl;
	that_record = record;
	return true;
      }
    }
  }
  return false;
}

bool find_read(seqan::Stream<seqan::Bgzf> &inStream, TBamIOContext &context, int rID, int this_pos, string this_qName, seqan::BamAlignmentRecord &record){
  //bool find_read(seqan::Stream<seqan::Bgzf> &inStream, int rID, int this_pos, string &this_qName ){
  while (!atEnd(inStream)) {
    assert (!readRecord(record, context, inStream, seqan::Bam())); 
    if (record.rID != rID || record.beginPos >= this_pos + 5) break;
    if (record.beginPos < this_pos-5 ) continue;
    if (record.qName == this_qName and record.beginPos == this_pos) return true;
  }
  return false;
}

RepMaskPos::RepMaskPos(string file_rmsk, int join_len){
  vector<string>::iterator ci;
  vector<int>::iterator pi;
  for (int i = 1; i < 23; i++)  chrns.push_back("chr" + int_to_string(i) );
  chrns.push_back("chrX");
  chrns.push_back("chrY");

  string line, chrn, chrn_pre="chr0";
  stringstream ss;
  int beginPos, endPos, beginPos_pre, endPos_pre;
  ifstream fin(file_rmsk.c_str());
  assert(fin);
  while (getline(fin, line)) {
    ss.clear(); ss.str( line );
    ss >> chrn >> beginPos >> endPos;
    if ( find(chrns.begin(), chrns.end(), chrn) == chrns.end() ) continue;
    if (chrn != chrn_pre) {
      chrn_pre = chrn;
      beginPos_pre = beginPos;
      endPos_pre = endPos;      
    } else {
      if (beginPos - endPos_pre > join_len) {  // close this block, create new
	beginP[chrn].push_back(beginPos_pre);
	endP[chrn].push_back(endPos_pre);	
	beginPos_pre = beginPos;
      }
      endPos_pre = endPos;      
    }
  }
  fin.close();    
//  for ( vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci++ ) {
//    assert(beginP[*ci].size() == endP[*ci].size() );
//    cerr << *ci << " " <<  beginP[*ci].size() << endl;
//  }
}

void RepMaskPos::print_begin(int ni){
  for ( vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci++ ) {
    cerr << *ci << " " << beginP[*ci].size() << endl;
    vector<int>::iterator pi = beginP[*ci].begin(); 
    vector<int>::iterator ei = endP[*ci].begin(); 
    for (int i = 0; i < ni and pi != beginP[*ci].end(); i++) 
      cerr << *pi++ << " " << *ei++ << endl;
  }
}

AluRefPosRead::AluRefPosRead(string file_alupos, int minLen) {
  ifstream fin( file_alupos.c_str());
  if (!fin) 
    try {
      throw 1;    
    } catch(int e) {
      cerr << "#### ERROR #### file: "<< file_alupos << " not exists!!!" << endl;
    }     
  string _tmp1, _tmp2, alu_type;
  char chain;
  int bp, ep;
  while (fin >> _tmp1 >> bp >> ep >> alu_type >> _tmp2 >> chain){
    if (ep - bp < minLen) continue;
    if (beginP.empty()) minP = bp;
    beginP.push(bp);
    endP.push(ep);
    strandP.push(chain);
    aluType.push(alu_type);
  }
  maxP = ep;
  cerr << "queue from " << file_alupos << " with " << beginP.size() << " loci, " << minP << " to " << maxP << endl;
  fin.close();
}

int AluRefPosRead::updatePos(int &beginPos, int &endPos){
  if (!beginP.size()) return 0;
  beginPos = beginP.front();
  endPos = endP.front();
  beginP.pop();
  endP.pop();
  return 1;
}  

int AluRefPosRead::updatePos(int &beginPos, int &endPos, char &chain, string & alu_type){
  if (!beginP.size()) return 0;
  beginPos = beginP.front();
  endPos = endP.front();
  chain = strandP.front();
  alu_type = aluType.front();
  beginP.pop();
  endP.pop();
  strandP.pop();
  aluType.pop();
  return 1;
}  

AluRefPos::AluRefPos(string file_alupos) {
  ifstream fin( file_alupos.c_str());
  assert(fin);
  string _tmp1, _tmp2, alu_type, _tmp3;
  int bp, ep;
  while (fin >> _tmp1 >> bp >> ep >> alu_type >> _tmp2 >> _tmp3){
    beginV.push_back(bp);
    endV.push_back(ep);
    typeV.push_back(alu_type);
  }
  fin.close();  
}



bool AluRefPos::insideAlu(int beginPos, int endPos, int alu_min_overlap, int &len_overlap){
  vector<int>::iterator bi, ei;
  for (bi = beginV.begin(), ei = endV.begin(); bi != beginV.end(); bi++, ei++){
    if ( *bi > endPos ) return false;
    if ( (len_overlap = min(*ei,endPos)-max(*bi, beginPos)) >= alu_min_overlap ) return true;
  }
  return false;
}

AluRefPos::~AluRefPos(void){
  beginV.clear();
  endV.clear();
  typeV.clear();
}


