#define SEQAN_HAS_ZLIB 1
#include <seqan/vcf_io.h>
#include "common.h"
#include "delete_utils.h"
#include "utils.h"
#include <sys/time.h>
#include <boost/timer.hpp>

#define COVERAGE_HIGH 100

inline string get_name_tmp(string path, string fn, string suffix){ return path + fn + suffix;}
inline string get_name_rg(string prefix, string pn){ return prefix + "RG." + pn;}
inline string get_name_rg_pdf(string prefix, string pn, string rg, string pdf_param){ return prefix + pn + ".count." + rg + "." + pdf_param; }

void count_reads(map <seqan::CharString, T_READ> &special_read, map < T_READ, int > &readCnt) {
  readCnt.clear();
  for (map <seqan::CharString, T_READ>::iterator rt = special_read.begin(); rt != special_read.end(); rt++) 
    addKey(readCnt, rt->second); 
}
 
int check_chr_alupos(seqan::Stream<seqan::Bgzf> &inStream, seqan::BamIndex<seqan::Bai> &baiIndex, seqan::FaiIndex &faiIndex, unsigned fa_idx, map <string, int> &rg_to_idx, TBamIOContext &context, int rID, int aluBegin, int aluEnd, unsigned coverage_max, float &coverage_mean, map <seqan::CharString, T_READ> &special_read,  string &rg_str){
  
  special_read.clear();
  stringstream rg_ss;

  bool hasAlignments = false;
  if (aluBegin <= ALU_FLANK  or !jumpToRegion(inStream, hasAlignments, context, rID, aluBegin-ALU_FLANK, aluEnd-ALU_FLANK, baiIndex)) return 0;
  if (!hasAlignments) return 0;
  int reads_cnt = 0;
  T_READ rt_val;
  seqan::BamAlignmentRecord record;
  while (!atEnd(inStream)) {
    assert (!readRecord(record, context, inStream, seqan::Bam())); 
    //  flank_bound -- alu_begin -- alu_end --(begin)-- flank_bound
    if (record.rID != rID || record.beginPos >= aluEnd + ALU_FLANK) break;
    if (record.beginPos + DEFAULT_READ_LEN < aluBegin - ALU_FLANK) continue;
    if ( !QC_delete_read(record) )  continue;    // ignore extreme large tLen 
    
    reads_cnt ++;  
    int align_len = getAlignmentLengthInRef(record);    
    bool read_is_left = left_read(record);
    if ( (!read_is_left) and special_read.find(record.qName) != special_read.end() ) continue;    	
    rt_val = classify_read( record, align_len, aluBegin, aluEnd, faiIndex, fa_idx, read_is_left);
    
    if ( rt_val != useless_read)  
      special_read[record.qName] = rt_val;
    if ( rt_val == unknow_read ) { // (unknow = jump across alu region)
	seqan::BamTagsDict tags(record.tags);
	unsigned idx_rg;
	assert (findTagKey(idx_rg, tags, "RG"));
	rg_ss << rg_to_idx[seqan::toCString(getTagValue(tags, idx_rg))] << ":" << abs(record.tLen) << " ";
    }	    
    /*    if ( rt_val == clip_read ) 
	  if ( rt_val == useless_read and (!read_is_left) )  
	  cout << "useless " << record.beginPos << " " << get_cigar(record) << " " << record.pNext << " " << record.tLen << endl;
    */
  }
  coverage_mean = length(record.seq) * (float) reads_cnt / (aluEnd - aluBegin + 2 * ALU_FLANK) ;
  if ( coverage_mean > coverage_max) return COVERAGE_HIGH;
  rg_str = rg_ss.str();
  return 1;
}

int delete_search( string & bam_input, string &bai_input, string file_fa_prefix, vector<string> &chrns, string &f_out, string &f_log, string &file_alupos_prefix, int coverage_max, map<string, int> &rg_to_idx) {    
  // Open BGZF Stream for reading.
  seqan::Stream<seqan::Bgzf> inStream;
  if (!open(inStream, bam_input.c_str(), "r")) {
    std::cerr << "ERROR: Could not open " << bam_input << " for reading.\n";
    return 1;
  }  
  seqan::BamIndex<seqan::Bai> baiIndex;
  if (read(baiIndex, bai_input.c_str()) != 0){
    cerr << "ERROR: Could not read BAI index file " << bai_input << endl;
    return 1;
  }
  TNameStore      nameStore;
  TNameStoreCache nameStoreCache(nameStore);
  TBamIOContext   context(nameStore, nameStoreCache);
  seqan::BamHeader header;
  seqan::BamAlignmentRecord record;
  assert(!readRecord(header, context, inStream, seqan::Bam()));
  seqan::FaiIndex faiIndex;
  unsigned fa_idx = 0;
  int aluBegin, aluEnd;      
  map < seqan::CharString, T_READ> special_read;  
  ofstream f_tmp1( f_out.c_str()); 
  f_tmp1 << "chr aluBegin aluEnd mean_coverage midCnt clipCnt unknowCnt unknowStr\n";
  ofstream f_log1( f_log.c_str());  // print out info for clip reads 

  for (vector<string>::iterator ci = chrns.begin(); ci!= chrns.end(); ci++) {
    string chrn = *ci;
    string file_alupos = file_alupos_prefix + chrn;
    AluRefPosRead *alurefpos = new AluRefPosRead(file_alupos, 200);
    int rID = 0;
    if (!getIdByName(nameStore, chrn, rID, nameStoreCache)) {
      cerr << "ERROR: Reference sequence named "<< chrn << " not known.\n";
      return 1;
    }    
    assert (!read(faiIndex, (file_fa_prefix + chrn + ".fa").c_str()) );      
    assert (getIdByName(faiIndex, chrn, fa_idx));
    for (int count_loci = 0; ; count_loci++) {
      float coverage_mean = 0;
      if (! alurefpos->updatePos(aluBegin, aluEnd)) break;      
      string rg_str;
      int check_alu = check_chr_alupos(inStream, baiIndex, faiIndex, fa_idx, rg_to_idx, context, rID, aluBegin, aluEnd, coverage_max, coverage_mean, special_read, rg_str);
      if ( check_alu == 0 ) continue;
      if ( check_alu == COVERAGE_HIGH) {
	f_log1 << "COVERAGE_HIGH " << chrn << " " << aluBegin << " " << aluEnd << " " << setprecision(2) << coverage_mean << endl;
	continue;
      }
      map < T_READ, int > readCnt;
      count_reads(special_read, readCnt); 
      if ( readCnt[clip_read] or readCnt[unknow_read] ) // not interesting if all are mid_reads
	f_tmp1 << chrn << " " << aluBegin << " " << aluEnd << " " << setprecision(2) << coverage_mean << " " <<  readCnt[mid_read]
	       << " " << readCnt[clip_read] << " " << readCnt[unknow_read] << " " << rg_str << endl;      
    }
    delete alurefpos;
    cerr << "file_alupos:done  " << file_alupos << endl;  
  }
  f_tmp1.close();
  f_log1.close();
  seqan::close(inStream);     
  return 0;
}


void calculate_geno_prob(){
  float *log_p = new float[3];
  delete log_p;
  /*
    int mid_read_count = 0, clip_read_count = 0, unknow_read_count = 0;
      count_reads(special_read, mid_read_count, clip_read_count);      
      // clip_read: evidence for deletion, mid_read: evidence for insertion
      log_p[0] = clip_read_count * (-LOG_RATIO_UB) ; 
      log_p[1] = (mid_read_count + clip_read_count) * log(0.5) ;
      log_p[2] = mid_read_count * (-LOG_RATIO_UB) ; 
      ///  other reads in rg_len      
      for ( rl = rg_len.begin(); rl != rg_len.end(); rl++) {  
	if (special_read.find(rl->first) != special_read.end()) continue;
	unknow_read_count ++;
	empiricalpdf = empiricalpdf_rg[(rl->second).first];
	int insert_len = (rl->second).second;
	float p_y = empiricalpdf->pdf_obs(insert_len);
	float p_z = empiricalpdf->pdf_obs(insert_len + aluEnd - aluBegin );
	log_p[0] += log(p_y);
	log_p[1] += log(0.67 * p_y + 0.33 * p_z);
	log_p[2] += log(p_z); 
      }	
      if ( (!unknow_read_count) and (!clip_read_count)) continue;
      if ( !normalize_prob(log_p) ) continue;
      */
     ///for (int i = 0; i < 3; i++)  f_tmp1 << log_p[i] << " " ;
}

bool combine_pns_count(vector <string> &pns, string path1, string f_out){
  map <string, MapII > del_count;
  map <string, MapII >::iterator dc;
  ifstream fin;
  stringstream ss;
  string line, chrn, tmp1, tmp2;
  int aluBegin, aluEnd;  
  float p0, p1, p2;
  for (vector <string>::iterator pi = pns.begin(); pi != pns.end(); pi++) {
    fin.open( get_name_tmp(path1, *pi, ".tmp1").c_str() );
    assert(fin);
    while ( getline(fin, line) ) {
      ss.clear(); ss.str( line );
      ss >> chrn >> tmp1 >> aluBegin >> aluEnd >> p0 >> p1 >> p2 ;
      if (p1 > p0 or p2 > p0) addKey(del_count[chrn], aluBegin, p1 > p2 ? 1 : 2); 
    }
    fin.close();
  }  
  //float pn_chr = 2. * pns.size();
  ofstream fout( f_out.c_str() );
  for (  dc = del_count.begin(); dc != del_count.end(); dc++ )
    for (MapIIt dc2 = (dc->second).begin(); dc2 != (dc->second).end(); dc2++)
      if (dc2->second > 1) fout << dc->first << " " << dc2->first << " " << dc2->second << endl;
  fout.close();
  return true;
}

string phred_log (float p) {
  if (p) return int_to_string ( -(int)(log10 (p) * 10));
  else return "255";
}

string phred_scaled(float p0, float p1, float p2){
  return phred_log(p0)  + "," + phred_log(p1) + "," + phred_log(p2);
}

void combine_pns_vcf(vector <string> &pns, string path1, string f_out, vector <string> &chrns){
  ifstream fin;
  stringstream ss;
  string line, chrn, tmp1, tmp2;
  int aluBegin, aluEnd, cii, flag;

  map < pair<int, int>, map<string, string> > pos_pn_prob;
  float p0, p1, p2;      
  map < pair<int, int>, map<string, string> >::iterator ppp;
  map<string, string>::iterator pp;
  vector <string>::iterator ci, pi;
  //seqan::VcfStream out("-", seqan::VcfStream::WRITE);
  seqan::VcfStream vcfout(seqan::toCString(f_out), seqan::VcfStream::WRITE);
  for ( ci = chrns.begin(); ci != chrns.end(); ci++)
    appendValue(vcfout.header.sequenceNames, *ci);
  for ( pi = pns.begin(); pi != pns.end(); pi++) 
    appendValue(vcfout.header.sampleNames, *pi);
  appendValue(vcfout.header.headerRecords, seqan::VcfHeaderRecord("fileformat", "VCFv4.1"));
  appendValue(vcfout.header.headerRecords, seqan::VcfHeaderRecord("fileDate", "201402"));
  appendValue(vcfout.header.headerRecords, seqan::VcfHeaderRecord("reference", "hg18"));
  appendValue(vcfout.header.headerRecords, seqan::VcfHeaderRecord("FILTER", "<ID=PL,Number=3,Type=Integer, Description=\"Phred-scaled likelihoods for genotypes\">"));
  seqan::VcfRecord record;    
  record.ref = ".";
  record.alt = "1";
  record.qual = 0;
  record.filter = ".";
  record.info = ".";
  record.format = ".";
    
  for ( cii = 0, ci = chrns.begin(); ci != chrns.end(); ci++, cii++) {
    pos_pn_prob.clear();
    for ( pi = pns.begin(); pi != pns.end(); pi++) {
      fin.open( get_name_tmp(path1, *pi, ".tmp1").c_str() );
      assert(fin);
      flag = 1;
      while ( getline(fin, line) ) {
	ss.clear(); ss.str( line );
	ss >> chrn >> tmp1 >> aluBegin >> aluEnd >> p0 >> p1 >> p2 ;
	if (chrn != *ci) {
	  if (flag) continue;
	  else break;
	}
	flag = 0;
	if (p1 > p0 or p2 > p0) pos_pn_prob[ make_pair(aluBegin, aluEnd) ][*pi] = phred_scaled(p0, p1, p2);
      }
      fin.close();
    }

    // Write out the records.
    for (ppp = pos_pn_prob.begin(); ppp != pos_pn_prob.end(); ppp++) {
      record.beginPos = (ppp->first).first;
      record.rID = cii;
      record.id = int_to_string((ppp->first).second);
      for (vector <string>::iterator pi = pns.begin(); pi != pns.end(); pi++) {
	if ( (pp = ppp->second.find(*pi)) != ppp->second.end() )  appendValue(record.genotypeInfos, pp->second);
	else  appendValue(record.genotypeInfos, "0,255,255"); 	  
      }
      writeRecord(vcfout, record);
      clear(record.genotypeInfos);
    }        
  }  // chrn finished
  clear(record);
  seqan::close(vcfout);
}

int main( int argc, char* argv[] )
{
  if (argc < 3) exit(1);

  int opt, idx_pn;
  seqan::lexicalCast2(opt, argv[1]);
  string config_file = argv[2];
  string path1 = read_config(config_file, "file_alu_delete0");
  if ( access( path1.c_str(), 0 ) != 0 ) system( ("mkdir " + path1).c_str() );
  
  vector<string> chrns;
  for (int i = 1; i < 23; i++)  chrns.push_back("chr" + int_to_string(i) );
  map<int, string> ID_pn;
  get_pn(read_config(config_file, "file_pn"), ID_pn);
  string file_fa_prefix = read_config(config_file, "file_fa_prefix");
  string file_dist_prefix = read_config(config_file, "file_dist_prefix");
  boost::timer clocki;    
  clocki.restart();

  if ( opt == 1 ) {  // write down counts 
    seqan::lexicalCast2(idx_pn, argv[3]);
    string chrn = argv[4];

    string pn = ID_pn[idx_pn];
    cerr << "reading pn: " << idx_pn << " " << pn << "..................\n";
    if (chrn != "chr0") { chrns.clear();  chrns.push_back(chrn); }
    map<string, int> rg_to_idx;
    int idx = 0; 
    ifstream fin;
    string rg;
    fin.open( get_name_rg(file_dist_prefix, pn).c_str());
    assert(fin);
    while (fin >> rg) rg_to_idx[rg] = idx++;
    fin.close();    
    unsigned coverage_max;
    seqan::lexicalCast2(coverage_max, (read_config(config_file, "coverage_max")));
    string bam_input = read_config(config_file, "file_bam_prefix") + pn + ".bam";
    string bai_input = bam_input + ".bai";  
    string file_alupos_prefix = read_config(config_file, "file_alupos_prefix"); 
    string fn_tmp1 = get_name_tmp(path1, pn, ".tmp1");
    string fn_log1 = get_name_tmp(path1, pn, ".log1");
    delete_search(bam_input, bai_input, file_fa_prefix, chrns, fn_tmp1, fn_log1, file_alupos_prefix, coverage_max, rg_to_idx);
    if ( chrn != "chr0") 
      return 0;
    // step 2: calculate prob
    string fn_tmp2 = get_name_tmp(path1, pn, ".tmp2");
    map <int, EmpiricalPdf *> empiricalpdf_rg;    
    string pdf_param = read_config(config_file, "pdf_param"); // 100_1000_5  
    fin.open( get_name_rg(file_dist_prefix, pn).c_str());
    while (fin >> rg) 
      empiricalpdf_rg[rg_to_idx[rg]] = new EmpiricalPdf( get_name_rg_pdf(file_dist_prefix, pn, rg, pdf_param));
    fin.close();
    // calculate_geno_prob(fn_tmp1, fn_tmp2, rg_to_idx, empiricalpdf_rg); 
    for (map <int, EmpiricalPdf *>::iterator ri = empiricalpdf_rg.begin(); ri != empiricalpdf_rg.end(); ri++) 
      //cout << ri->first << " " << (ri->second)->pdf_obs(300) << " " << (ri->second)->pdf_obs(400) << endl;
      delete ri->second;
    
  } else if (opt == 2) {   // filter candidate
    vector <string> pns;
    ifstream fin(read_config(config_file, "file_pnIdx_used").c_str());
    while (fin >> idx_pn) pns.push_back( ID_pn[idx_pn]);
    fin.close();
    //print_vec(pns);
    string f_out = path1+"test_pn";
    //combine_pns_count(pns, path1, f_out); 
    f_out = path1+"test_vcf";
    combine_pns_vcf(pns, path1, f_out, chrns);
    cerr << "output to " << f_out << endl;

  } else if (opt == 3) { // debugging and manually check some regions 
    string pn = "AACXPPZ";
    string chrn = "chr1";
    string bam_input = read_config(config_file, "file_bam_prefix") + pn + ".bam";
    string bai_input = bam_input + ".bai";  
    string fa_input = file_fa_prefix + chrn + ".fa";
    
    //check_delete_region(bam_input, bai_input, fa_input, chrn, 62163190 - 600,  62163528 + 600);
    //check_delete_region(bam_input, bai_input, fa_input, chrn, 1514448 - 600,  1514649 + 600);
  }

  cout << "time used " << clocki.elapsed() << endl;
  return 0;  
}
