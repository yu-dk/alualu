#define SEQAN_HAS_ZLIB 1
#include "insert_utils.h"
#include <seqan/basic.h>
#include <sys/time.h>
#include <boost/timer.hpp>

inline string get_name_pos1(string prefix, string chrn){ return prefix + chrn + ".highCov"; }

bool coverage_idx_pass(string line, int &refBegin, int &refEnd, int idx_pn_this, float freq_min, float freq_max, int pn_cnt, bool & maf_pass, stringstream & ss_highCov){
  int n_reads, idx_pn;
  float coverage;
  stringstream ss;
  ss.str(line);
  int n_pn = 0, flag = 0;
  ss >> n_reads >> refBegin >> refEnd;
  while ( ss >> idx_pn) {
    n_pn ++;
    if (idx_pn_this == idx_pn) flag = 1;
  }
  coverage = n_reads * DEFAULT_READ_LEN / (float) n_pn / (float)(refEnd - refBegin + SCAN_WIN_LEN * 2);
  if ( idx_pn_this == 0 and coverage >= INS_COVERAGE_MAX ) 
    ss_highCov << refBegin << " " << refEnd << endl;

  float n_freq = (float)n_pn / pn_cnt; 
  maf_pass = (n_freq <= freq_max and n_freq >= freq_min);
  return maf_pass and coverage < INS_COVERAGE_MAX and flag ;
}

// write all reads that is useful for split mapping or inferring insert sequence !
void reads_insert_loci(int idx_pn, vector<string> & chrns, string bam_input, string bai_input, string fin_pos, string fout_reads_fa, float freq_min, float freq_max, ofstream &fout_pos, int pn_cnt){
  BamFileHandler *bam_fh = new BamFileHandler(chrns, bam_input, bai_input); 
  ofstream frecord(fout_reads_fa.c_str());
  frecord << "chrn region_begin region_end beginPos endPos cigar hasFlagRC seq\n" ;
  int region_begin, region_end;
  string line;
  for (map<int, string>::iterator rc = bam_fh->rID_chrn.begin(); rc != bam_fh->rID_chrn.end(); rc++) {
    string chrn = rc->second; 
    ifstream fin( (fin_pos + chrn).c_str());
    stringstream ss_highCov;
    if (!fin) continue;  // ignore random chr
    bool maf_pass;
    bool loci_pass;
    while (getline(fin, line)) {
      loci_pass = coverage_idx_pass(line, region_begin, region_end, idx_pn, freq_min, freq_max, pn_cnt, maf_pass, ss_highCov);
      if ( maf_pass and !idx_pn) fout_pos << line << endl;
      if ( !loci_pass) continue;
      int refBegin, refEnd;
      refPos_by_region(region_begin, region_end, refBegin, refEnd);  
      if (! bam_fh->jump_to_region(chrn, refBegin, refEnd) ) continue;
      seqan::BamAlignmentRecord record;
      while ( true ) {
	string read_status = bam_fh->fetch_a_read(chrn, refBegin, refEnd, record);
	if (read_status == "stop" ) break;
	if (read_status == "skip" or !QC_insert_read(record)) continue;
	if ( has_soft_last(record, CLIP_BP) or has_soft_first(record, CLIP_BP) ) 
	  frecord << chrn << " " << region_begin << " " << region_end << " " << record.beginPos << " " 
		  << record.beginPos + getAlignmentLengthInRef(record) << " " 
		  << get_cigar(record) << " " << hasFlagRC(record) << " " << record.seq << endl;	    
      }
    }
    fin.close();
    if (!idx_pn) {
      ofstream fPos;
      fPos.open( get_name_pos1(fin_pos, chrn).c_str());
      fPos << ss_highCov.str();
      fPos.close();
      ss_highCov.clear();
    }
  }
  delete bam_fh;
}

void read_file_pn_used(string fn, set<string> & pns_used) {
  ifstream fin(fn.c_str()) ;
  assert(fin);
  string pn;
  while (fin >> pn) pns_used.insert(pn);
  fin.close();
}

void read_file_pn_used(string fn, set<int> & ids_used, map<string, int> & pn_ID) {
  ifstream fin(fn.c_str()) ;
  assert(fin);
  string pn;
  while (fin >> pn) ids_used.insert(pn_ID[pn]);
  fin.close();
}

float major_key_freq(map <int, int> & m, int & key)  {
  if ( m.empty() ) return 2.0; 
  int max_count = 0, sum_count = 0;
  key = m.begin()->first;
  for (map <int, int>::iterator it = m.begin(); it != m.end(); it++) { 
    sum_count +=  it->second;
    if ( it->second > max_count ) {
      max_count = it->second;
      key = it->first;
    }
  }
  //cout << "mk_freq " << max_count/(float) sum_count << endl;
  return max_count/(float) sum_count;
}

float left_major_pos(map<int, int> &m, int pos_dif, int &pos) {
  if ( m.empty() ) return 0;
  map <int, int>::iterator it = m.begin();
  pos = it->first;
  int max_count = m[pos];
  int sum_count = max_count;
  for (; it != m.end(); it++) { 
    sum_count +=  it->second;
    if ( it->first <= pos + pos_dif) max_count += it->second;    
  }
  return max_count/(float) sum_count;
}

float right_major_pos(map<int, int> &m, int pos_dif, int &pos) {
  if ( m.empty() ) return 0;
  int max_count, sum_count;
  map <int, int>::iterator it;
  for (it = m.begin(); it != m.end(); it++) { pos = it->first; max_count = it->second;}
  sum_count = max_count;
  size_t i;
  for (i = 0, it = m.begin(); i < m.size() - 1; i++, it++) {
    sum_count +=  it->second;
    if ( pos - it->first <= pos_dif ) max_count += it->second;
  }
  return max_count/(float) sum_count;
}

bool pn_has_clipPos( int & clipLeft, int & clipRight, vector <pair<char, int> > & splitori_pos, float major_freq) {
  clipLeft = 0; clipRight = 0;
  map <int, int> clipLefts, clipRights;
  for ( vector <pair<char, int> >::iterator si = splitori_pos.begin(); si != splitori_pos.end(); si++) {
    if ((*si).first == 'L') addKey(clipLefts, (*si).second);
    else if ((*si).first == 'R') addKey(clipRights, (*si).second);
  }

  // both left and right major exists(or missing)
  float clipLeft_freq = major_key_freq(clipLefts, clipLeft);
  float clipRight_freq = major_key_freq(clipRights, clipRight);
  
  if (clipLeft_freq > 1 and clipRight_freq > 1) 
    return false;

  if (clipLeft_freq >= major_freq and clipRight_freq >= major_freq) {
    if (!clipLeft) clipLeft = clipRight;
    if (!clipRight) clipRight = clipLeft;
    if (abs(clipRight - clipLeft) > INSERT_POS_GAP) return false;    
    if ( clipRight < clipLeft ) clipRight = clipLeft;
    return clipLeft > 0;
  }
  return false;
}

int pn_clipRight(vector <pair<char, int> > & splitori_pos, float major_freq) {
  map <int, int> clipRights;
  int clipRight;
  vector <pair<char, int> >::iterator si;
  for (si = splitori_pos.begin(); si != splitori_pos.end(); si++) 
    if ((*si).first == 'R') addKey(clipRights, (*si).second);
  if (major_key_freq(clipRights, clipRight) >= major_freq and clipRight > 0 )
    return clipRight;
  return 0;
}

/** move to left until can't move */
bool clipRight_move_left(string & read_seq, seqan::CharString & ref_fa, list <int> & cigar_cnts, int refBegin, int & _clipRight) {
  int i = 1;  // i = 0 is the last match by BWA
  while ( *cigar_cnts.begin() - i >= 0 and 
	  ref_fa[_clipRight - refBegin - i] == read_seq[*cigar_cnts.begin() - i] )
    i++;
  if ( *cigar_cnts.begin() - (i-1) < CLIP_BP )
    return false;
  _clipRight -= (i-1);  
  return true;  
}

/** move to right until can't move */
bool clipLeft_move_right(string & read_seq, seqan::CharString & ref_fa, list <int> & cigar_cnts, int refBegin, int & _clipLeft, const int clipLeft_max){
  int align_len = read_seq.size() - cigar_cnts.back();
  int i = 0;
  while (align_len + i < (int)read_seq.size() and ref_fa[_clipLeft - refBegin + i] == read_seq[align_len + i] and _clipLeft + i <= clipLeft_max ) i++;
  if ( cigar_cnts.back() - i < CLIP_BP)
    return false;
  if ( clipLeft_max > 0 ) _clipLeft = min( _clipLeft + i, clipLeft_max);     
  else _clipLeft += i;
  return true;  
}

bool insert_pos_exists(FastaFileHandler *fasta_fh, string file_clip, int region_begin, int region_end, int flanking_len, float major_freq, int & clipLeft, int & clipRight) {
  stringstream ss;
  string line, pn, cigar, seq, read_seq;
  int beginPos, endPos, hasRCFlag, _clipRight, _clipLeft;
  map <string, vector <pair<char, int> > > pn_splitori_pos;
  
  ifstream fin;
  fin.open(file_clip.c_str());
  while ( getline(fin, line)) {
    ss.clear(); ss.str(line); 
    ss >> pn >> beginPos >> endPos >> cigar >> hasRCFlag >> read_seq;
    list <char> cigar_opts;
    list <int> cigar_cnts;
    parse_cigar(cigar, cigar_opts, cigar_cnts);
    /** one read can have strange cigar (eg S35M22S63), due to error. */    
    int refBegin = region_begin - 200;
    int refEnd = region_end + 200;
    seqan::CharString ref_fa;
    fasta_fh->fetch_fasta_upper(refBegin, refEnd, ref_fa);
    // splitEnd, S*M*
    if ( *cigar_opts.begin() == 'S' and *cigar_cnts.begin() >= CLIP_BP and
	 beginPos >= region_begin - flanking_len and beginPos < region_end + flanking_len) {      
      _clipRight = beginPos;                  
      if (clipRight_move_left(read_seq, ref_fa, cigar_cnts, refBegin, _clipRight) )
	pn_splitori_pos[pn].push_back( make_pair('R', _clipRight) );            
    }
  }
  fin.close();

  fin.open(file_clip.c_str());
  while ( getline(fin, line)) {
    ss.clear(); ss.str(line); 
    ss >> pn >> beginPos >> endPos >> cigar >> hasRCFlag >> read_seq;
    list <char> cigar_opts;
    list <int> cigar_cnts;
    parse_cigar(cigar, cigar_opts, cigar_cnts);
    int refBegin = region_begin - 200;
    int refEnd = region_end + 200;
    seqan::CharString ref_fa;
    fasta_fh->fetch_fasta_upper(refBegin, refEnd, ref_fa);
    // splitBegin, M*S*
    if ( cigar_opts.back() == 'S' and cigar_cnts.back() >= CLIP_BP and
	 endPos >= region_begin - flanking_len and endPos < region_end + flanking_len) {
      _clipLeft = endPos;
      if (pn_splitori_pos.find(pn) != pn_splitori_pos.end() ) 
	_clipRight = pn_clipRight(pn_splitori_pos[pn], major_freq);
      else
	_clipRight = 0;
      if (clipLeft_move_right(read_seq, ref_fa, cigar_cnts, refBegin, _clipLeft, _clipRight))
	pn_splitori_pos[pn].push_back( make_pair('L', _clipLeft) );
    }
  }
    
  // check each pn
  map <int, int> clipLefts, clipRights;
  map <string, vector <pair<char, int> > >::iterator pi; 
  for (pi = pn_splitori_pos.begin(); pi != pn_splitori_pos.end(); pi++) {    
    if (pn_has_clipPos(_clipLeft, _clipRight, pi->second, major_freq)) {
      addKey(clipLefts, _clipLeft);
      addKey(clipRights, _clipRight);
    }    
  }  
  
  clipLeft = clipRight = 0;
  float clipLeft_freq = major_key_freq(clipLefts, clipLeft);
  float clipRight_freq = major_key_freq(clipRights, clipRight);
  if (clipLeft_freq > 1 or clipRight_freq > 1) 
    return false;
    
  return ( clipLeft_freq >= major_freq and clipRight_freq >= major_freq );
  
  /*
    for (pi = pn_splitori_pos.begin(); pi != pn_splitori_pos.end(); pi++) 
    for (vector <pair<char, int> >::iterator pii = (pi->second).begin(); pii != (pi->second).end(); pii++) 
    if ( ( (*pii).first == 'L' and abs((*pii).second - clipLeft) <= 3 ) or 
    ( (*pii).first == 'R' and abs((*pii).second - clipRight) <= 3 ) ) 
    addKey(pn_splitCnt, pi->first);      
  */   
}

void read_insert_pos(string file_input, map< pair<int, int>, vector<string> > & insertPos_fn){
  ifstream fin(file_input.c_str());
  //if (!fin) cout << "error " << file_input << " is missing\n";
  assert(fin);
  string line, region_begin, region_end;
  int clipLeft, clipRight, pre_clipLeft = 0;
  getline(fin, line);
  while ( fin >> region_begin >> region_end >> clipLeft >> clipRight ) {
    if ( pre_clipLeft and pre_clipLeft != clipLeft)
      insertPos_fn[make_pair(clipLeft, clipRight)].push_back(region_begin+"_"+region_end);
    pre_clipLeft = clipLeft;
  }
  fin.close();
}

bool global_align(int hasRCFlag, seqan::CharString seq_read, seqan::CharString seq_ref, int &score, int cutEnd = 15){
  score = 0;
  size_t align_len = length(seq_read);
  if ( align_len != length(seq_ref) ) return false;
  if ( align_len <= CLIP_BP) return false;
  seqan::Score<int> scoringScheme(1, -2, -2, -2); // match, mismatch, gap extend, gap open
  TAlign align;
  resize(rows(align), 2);
  assignSource(row(align,0), seq_read); // 2,3, true means free gap
  assignSource(row(align,1), seq_ref);   // 1,4
  if (!hasRCFlag)
    score = globalAlignment(align, scoringScheme, seqan::AlignConfig<false, false, true, true>()); 
  else
    score = globalAlignment(align, scoringScheme, seqan::AlignConfig<true, true, false, false>());   
  if (score >= round(0.7 * align_len)  )
    return true;
//  cout << "score1 "<< score << " " << round(0.7 * align_len) << endl;
//  cout << align << endl;
  
  if ( (int)align_len <= CLIP_BP + cutEnd) return false;
  if (!hasRCFlag){
    assignSource(row(align,0), infix(seq_read, 0, align_len - cutEnd )); 
    assignSource(row(align,1), infix(seq_ref, 0, align_len - cutEnd ));  
  } else {
    assignSource(row(align,0), infix(seq_read, cutEnd, align_len)); 
    assignSource(row(align,1), infix(seq_ref, cutEnd, align_len));      
  }
  score = globalAlignment(align, scoringScheme, seqan::AlignConfig<false, false, false, false>()); 
//  cout << "score2 "<< score << " " << round(0.7 * (align_len - cutEnd)) << endl;
//  cout << align << endl;
  return score >= round(0.7 * (align_len - cutEnd)) ;
}

void pn_with_insertion(map <string, int> & pn_splitCnt, FastaFileHandler *fasta_fh, string file_clip, int clipLeft, int clipRight) {
  stringstream ss;
  string line, pn, cigar, seq, read_seq;
  int beginPos, endPos, hasRCFlag;
  ifstream fin;
  fin.open(file_clip.c_str());
  assert(fin);
  while ( getline(fin, line)) {
    ss.clear(); ss.str(line); 
    ss >> pn >> beginPos >> endPos >> cigar >> hasRCFlag >> read_seq;
    list <char> cigar_opts;
    list <int> cigar_cnts;
    parse_cigar(cigar, cigar_opts, cigar_cnts);
    int read_seq_size = read_seq.size();
    int refFlank = read_seq_size + 10;
    seqan::CharString ref_fa; 
    fasta_fh->fetch_fasta_upper(clipLeft - refFlank, clipRight + refFlank, ref_fa);
    int adj_bp, align_len, score;
    
    // cigar string is not perfect 
    if ( *cigar_opts.begin() == 'S' and abs(adj_bp = beginPos - clipRight) < 30 ) {
      int read_begin = *cigar_cnts.begin() - adj_bp;
      if (read_begin > 0 and read_begin <= read_seq_size - CLIP_BP) {
	align_len = read_seq_size - read_begin;
	int ref_begin = clipRight - clipLeft + refFlank ;
	//cout << "SM " << cigar <<  " " << read_seq.substr(read_begin, align_len) << endl; 
	//cout << "SM " << cigar <<  " "<< infix(ref_fa, ref_begin, ref_begin + align_len) << endl;      
	if ( global_align(hasRCFlag, read_seq.substr(read_begin, align_len), 
			  infix(ref_fa, ref_begin, ref_begin + align_len), score))  {
	  addKey(pn_splitCnt, pn);
	  //cout << "cigar " << cigar << endl;
	  continue;
	}
      }	
    }
    if ( cigar_opts.back() == 'S' and abs(adj_bp = endPos - clipLeft) < 30 ) {  
      align_len = read_seq_size - cigar_cnts.back() + adj_bp;
      if (align_len < CLIP_BP or align_len >= read_seq_size) continue;
      //cout << "MS " << cigar << " " << read_seq.substr(0, align_len) << endl;
      //cout << "MS " << cigar << " " << infix(ref_fa, refFlank - align_len, refFlank) << endl;      
      if ( global_align(hasRCFlag, read_seq.substr(0, align_len), 
			infix(ref_fa, refFlank - align_len, refFlank), score )) {
	addKey(pn_splitCnt, pn);
      } 
    }    
  }
  fin.close();
}

int main( int argc, char* argv[] )
{
  string config_file = argv[1];
  string opt = argv[2];
  if (argc < 4) exit(1);

  boost::timer clocki;    
  clocki.restart();  

  ConfigFileHandler cf_fh = ConfigFileHandler(config_file);
  string path1 = cf_fh.get_conf( "file_alu_insert1") ;    
  string fout_path = cf_fh.get_conf( "file_clip_reads");
  check_folder_exists(fout_path);
  vector<string> chrns;
  for (int i = 1; i < 23; i++) chrns.push_back("chr" + int_to_string(i));
  chrns.push_back("chrX");
  chrns.push_back("chrY");
  
  string tmp_path;
  for (vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci ++) {
    string chrn = *ci;
    tmp_path = fout_path + chrn + "_pos/";
    check_folder_exists( tmp_path);
    tmp_path = fout_path + chrn + "/";
    check_folder_exists( tmp_path );      
  }

#ifdef DEBUG_MODE   // only look at chr1 for now 
    chrns.clear();
    chrns.push_back("chr1");
#endif       

  map<int, string> ID_pn;
  get_pn(cf_fh.get_conf( "file_pn"), ID_pn);      
  string file_pn_used = path1 + "pn.insert_pos";
  string fin_pos = path1 + "insert_pos.";
  float freq_min = seqan::lexicalCast<float> (cf_fh.get_conf("freq_min"));
  float freq_max = seqan::lexicalCast<float> (cf_fh.get_conf("freq_max"));
  stringstream ss;
  ss << "_" << setprecision(3) << freq_min << "_" << setprecision(3) << freq_max;
  string fout_suffix = ss.str();
  ss.clear();
  
  if (opt == "1") { // less than 2 to 60 mins for each pn.
    int idx_pn = seqan::lexicalCast<int> (argv[3]);
    string pn = ID_pn[idx_pn];    
    cerr << "reading " << pn << endl;
    string bam_input = cf_fh.get_conf( "file_bam_prefix") + pn + ".bam";
    string bai_input = bam_input + ".bai";      
    ofstream fout_pos;
    set<string> pns_used;
    read_file_pn_used(file_pn_used, pns_used); // some pn is ignored, due to too many reads
    for (vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci++) {      
      if ( idx_pn and pns_used.find(pn) == pns_used.end() ) continue; 
      if (!idx_pn)  fout_pos.open( (fin_pos + *ci + fout_suffix).c_str());
      string fout_reads_fa = fout_path + *ci + "/" + pn + fout_suffix;
      reads_insert_loci(idx_pn, chrns, bam_input, bai_input, fin_pos, fout_reads_fa, freq_min, freq_max, fout_pos, ID_pn.size() );
      if (!idx_pn) fout_pos.close();
    }
  } else if ( opt == "2" ) { // for each insertion loci, combine reads from different pns 
    fstream fout;
    set<string> pns_used;
    read_file_pn_used(file_pn_used, pns_used);
    for (vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci++) {
      set<string> beginPos_fn;
      for (set<string>::iterator pi = pns_used.begin(); pi != pns_used.end(); pi ++ ) {
	string file_input_clip, file_output_clip;
	file_input_clip = fout_path + *ci + "/" + *pi + fout_suffix;
	ifstream fin(file_input_clip.c_str());
	if (!fin) {
	  cout << "error " << file_input_clip <<  " missing \n";
	  continue;
	}	
	stringstream ss;
	string line, tmp1, cigar, seq, region_begin, region_end, beginPos, endPos, hasFlagRC;
	getline(fin, line);
	string region_begin_pre="";
	while ( getline(fin, line) ) {
	  ss.clear(); ss.str(line); 
	  ss >> tmp1 >> region_begin >> region_end >> beginPos >> endPos >> cigar >> hasFlagRC >> seq;
	  file_output_clip = fout_path + *ci + "_pos/" + region_begin + "_" + region_end;
	  if (region_begin != region_begin_pre) {
	    region_begin_pre = region_begin;
	    fout.close();
	    if ( beginPos_fn.find(region_begin) == beginPos_fn.end() ) {
	      fout.open(file_output_clip.c_str(), fstream::out);
	      beginPos_fn.insert(region_begin);
	    } else {
	      fout.open(file_output_clip.c_str(), fstream::app|fstream::out);
	    }
	  }
	  fout << *pi << " " << beginPos << " " << endPos << " " << cigar << " " << hasFlagRC << " " << seq << endl;
	}	  
	fin.close();
      }
    }    
  } else if ( opt == "3" ) {  // find exact split position
    string file_fa_prefix = cf_fh.get_conf( "file_fa_prefix");    
    string cnt, line;
    stringstream ss;
    for (vector<string>::iterator ci = chrns.begin(); ci != chrns.end(); ci++) {
      FastaFileHandler *fasta_fh = new FastaFileHandler(file_fa_prefix + *ci + ".fa", *ci);
      ofstream fout;
      ifstream fin( (fin_pos + *ci + fout_suffix).c_str());
      assert (fin);
      string file_insert_pos = path1 + "clip/" + *ci + fout_suffix;
      fout.open ( (file_insert_pos + ".tmp").c_str() ); 
      fout << "region_begin region_end clipLeft clipRight\n";
      int region_begin, region_end;
      while ( getline(fin, line) ) {
	ss.clear(); ss.str(line); 
	ss >> cnt >> region_begin >> region_end;
	string file_clip = fout_path + *ci + "_pos/" + int_to_string(region_begin) + "_" + int_to_string(region_end);
	int clipLeft, clipRight;
	if ( is_nonempty_file(file_clip) <= 0) continue;		
	if ( insert_pos_exists(fasta_fh, file_clip, region_begin, region_end, FLANK_REGION_LEN, MAJOR_SPLIT_POS_FREQ, clipLeft, clipRight) )
	  fout << region_begin << " " << region_end << " " << clipLeft << " " << clipRight << endl;
      }
      fout.close();
      cout << "position file written into " << file_insert_pos + ".tmp" << endl;

      cout << "NB: private insertion is ignored !\n";    
      map< pair<int, int>, vector<string> > insertPos_fileNameOfReads;
      read_insert_pos(file_insert_pos + ".tmp", insertPos_fileNameOfReads);
      fout.open(file_insert_pos.c_str()); 
      fout << "clipLeft clipRight pn(count)\n";
      cout << insertPos_fileNameOfReads.size() << " files to read\n";
      map< pair<int, int>, vector<string> >::iterator ii;
      for (ii = insertPos_fileNameOfReads.begin(); ii != insertPos_fileNameOfReads.end(); ii++){
	map <string, int> pn_splitCnt;      	
	for (vector<string>::iterator fi = ii->second.begin(); fi != ii->second.end(); fi++) {
	  string file_clip = fout_path + *ci + "_pos/" + *fi;
	  pn_with_insertion(pn_splitCnt, fasta_fh, file_clip, (ii->first).first, (ii->first).second);
	}
	if (pn_splitCnt.size() <= 1) continue;
	fout << (ii->first).first << " " << (ii->first).second;
	for (map<string, int>::iterator pni = pn_splitCnt.begin(); pni != pn_splitCnt.end(); pni++)
	  fout << " " << pni->first << "," << pni->second;
	fout << endl;
      }
      fout.close();
      delete fasta_fh;
    }
  } else if ( opt == "debug" ) {  
    string file_clip;
    seqan::FaiIndex faiIndex;
    unsigned fa_idx = 0;    
    string file_fa_prefix = cf_fh.get_conf( "file_fa_prefix");    
    assert (!read(faiIndex, (file_fa_prefix + "chr1.fa").c_str()) );
    assert (getIdByName(faiIndex, "chr1", fa_idx));
    file_clip = fout_path + "chr1_pos/7000778_7000978";
    //int clipLeft = 7000791;
    //int clipRight = 7000791;

    // int clipLeft = 7000703;
    // int clipRight = 7000703;
    // map <string, int> pn_splitCnt;      	
    // pn_with_insertion(pn_splitCnt, faiIndex, fa_idx, file_clip, clipLeft, clipRight);
    // cout << pn_splitCnt.size() << endl;
//    for (map<string, int>::iterator pni = pn_splitCnt.begin(); pni != pn_splitCnt.end(); pni++)
//      fout << " " << pni->first << "," << pni->second;
//    fout << endl;
  }
  return 0;
}
