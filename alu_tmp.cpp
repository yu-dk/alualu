#define SEQAN_HAS_ZLIB 1
#include "delete_utils.h"

void count_reads(map <seqan::CharString, T_READ> &qName_info, map < T_READ, int > &readCnt) {
  readCnt.clear();
  for (map <seqan::CharString, T_READ>::iterator rt = qName_info.begin(); rt != qName_info.end(); rt++) 
    addKey(readCnt, rt->second); 
}
 
int check_one_pos(BamFileHandler* bam_fh, FastaFileHandler *fasta_fh, map <string, int> &rg_to_idx, string chrn, int aluBegin, int aluEnd, unsigned coverage_max, float &coverage_mean, map <seqan::CharString, T_READ> &qName_info,  map<seqan::CharString, string> & rg_str){  
  qName_info.clear();
  int reads_cnt = 0;
  seqan::BamAlignmentRecord record;  
  while ( true ) {
    string read_status = bam_fh->fetch_a_read(chrn, aluBegin - ALU_FLANK, aluEnd + ALU_FLANK, record);
    if (read_status == "stop" ) break;
    if (read_status == "skip" or !QC_delete_read(record)) continue;
    reads_cnt ++;  
    map <seqan::CharString, T_READ >::iterator qItr = qName_info.find(record.qName);
    if ( qItr != qName_info.end() and qItr->second != unknow_read) 
      continue;
    T_READ iread = classify_read( record, aluBegin, aluEnd, fasta_fh);   
    if ( iread == useless_read) continue;    
    qName_info[record.qName] = iread;
    if ( qItr == qName_info.end() and iread == unknow_read) {
      int rgIdx = get_rgIdx(rg_to_idx, record);
      stringstream rg_ss;
      rg_ss << rgIdx << ":" << abs(record.tLen);
      rg_str[record.qName] = rg_ss.str();
    }    
  }
  coverage_mean = length(record.seq) * (float) reads_cnt / (aluEnd - aluBegin + 2 * ALU_FLANK) ;
  if ( coverage_mean > coverage_max) return COVERAGE_HIGH;
  return 1;
}

int delete_search(int minLen_alu_del, BamFileHandler *bam_fh, string file_fa_prefix, vector<string> &chrns, string &f_out, string &f_log, string &file_alupos_prefix, int coverage_max, map<string, int> &rg_to_idx) {    
  map < seqan::CharString, T_READ> qName_info;  
  ofstream f_tmp1( f_out.c_str()); 
  f_tmp1 << "chr aluBegin aluEnd mean_coverage midCnt clipCnt unknowCnt unknowStr\n";
  ofstream f_log1( f_log.c_str());  // print out info for clip reads 
  for (vector<string>::iterator ci = chrns.begin(); ci!= chrns.end(); ci++) {
    string chrn = *ci;
    string file_alupos = file_alupos_prefix + chrn;
    AluRefPos *alurefpos = new AluRefPos(file_alupos, minLen_alu_del); // default 200
    FastaFileHandler *fasta_fh = new FastaFileHandler(file_fa_prefix + chrn + ".fa", chrn);    
    int aluBegin, aluEnd;
    for (int count_loci = 0; ; count_loci++) {
      float coverage_mean = 0;
      if ( !alurefpos->nextdb() ) break;      
      aluBegin = alurefpos->get_beginP();
      aluEnd = alurefpos->get_endP();
      if (aluBegin <= ALU_FLANK or !bam_fh->jump_to_region(chrn, aluBegin-ALU_FLANK, aluEnd + ALU_FLANK))
	continue;
      map<seqan::CharString, string> rg_str;
      int check_alu = check_one_pos(bam_fh, fasta_fh, rg_to_idx, chrn, aluBegin, aluEnd, coverage_max, coverage_mean, qName_info, rg_str);
      if ( check_alu == 0 ) continue;
      if ( check_alu == COVERAGE_HIGH) {
	f_log1 << "COVERAGE_HIGH " << chrn << " " << aluBegin << " " << aluEnd << " " << setprecision(2) << coverage_mean << endl;
	continue;
      }
      map < T_READ, int > readCnt;
      count_reads(qName_info, readCnt); 
      if ( readCnt[clip_read] or readCnt[unknow_read] ) {// not interesting if all are mid_reads
	f_tmp1 << chrn << " " << aluBegin << " " << aluEnd << " " << setprecision(2) << coverage_mean << " " <<  readCnt[mid_read]
	       << " " << readCnt[clip_read] << " " << readCnt[unknow_read];
	for (map<seqan::CharString, string>::iterator ri = rg_str.begin(); ri != rg_str.end(); ri++) 
	  if (qName_info[ri->first] == unknow_read)
	    f_tmp1 << " " << ri->second ;
	f_tmp1 << endl;
      }
    }
    delete alurefpos;
    delete fasta_fh;
    cout << "file_alupos:done  " << file_alupos << endl;  
  }
  f_tmp1.close();
  f_log1.close();
  return 0;
}

bool parseline_del_tmp1(string &line, string & output_line, map <int, EmpiricalPdf *> & pdf_rg){
  float *log10_gp = new float[3];
  stringstream ss, ss_out;
  string chrn, meanCov;
  int aluBegin, aluEnd, midCnt, clipCnt, unknowCnt;
  ss.clear(); ss.str(line); 
  ss >> chrn >> aluBegin >> aluEnd >> meanCov >> midCnt >> clipCnt >> unknowCnt ;
  float prob_ub = pow(10, -LOG10_RATIO_UB);
  float *gp = new float[3];

  for (int i = 0; i < 3; i++) log10_gp[i] = 0;
  if (clipCnt + midCnt > 0) {
    log10_gp[0] = clipCnt * log10 ( prob_ub ) + midCnt * log10 ( (1 - prob_ub) );
    log10_gp[1] = (midCnt + clipCnt) * log10 (0.5) ; 
    log10_gp[2] = midCnt * log10 ( prob_ub ) + clipCnt * log10 ( (1 - prob_ub) );
  }

  log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);  // normalize such that sum is 1
  cout << "debug1## " << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2] << endl; 
  
  float freq0 = ( midCnt + 1 )/(float)(midCnt + clipCnt + 2); // 1 and 2 are psudo count
  if (unknowCnt) { 
    int insert_len, idx;
    string token;
    for (int i = 0; i < unknowCnt; i++) {
      getline(ss, token, ':');
      seqan::lexicalCast2(idx, token);
      getline(ss, token, ' ');
      seqan::lexicalCast2(insert_len, token);      
      float p_y = pdf_rg[idx]->pdf_obs(insert_len);
      float p_z = pdf_rg[idx]->pdf_obs(insert_len - aluEnd + aluBegin);
      //freq0 = 0.67;  // high FP ratio      
      log10_gp[0] += log10 (p_y);
      log10_gp[1] += log10 ((freq0 * p_y + (1 - freq0) * p_z)) ;
      log10_gp[2] += log10 (p_z);
    }
   }
  
  log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);  // normalize such that sum is 1
  cout << "debug2## " << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2] << endl;   

  log10_gp[0] +=  2 * log10 (freq0);
  log10_gp[1] +=  log10 ( 2 * freq0 * (1-freq0) );
  log10_gp[2] +=  2 * log10 ( 1-freq0);

  log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);  // normalize such that sum is 1
  cout << "debug3## " << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2] << endl;   
  
  bool use_this_line = false;
  if ( !p00_is_dominant(log10_gp, - LOG10_GENO_PROB) ) {
    ss_out << chrn << " " << aluBegin << " " << aluEnd << " " << midCnt << " " << clipCnt << " " << unknowCnt ;
    log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);  // normalize such that sum is 1
    ss_out << " " << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2]; 
    output_line = ss_out.str();
    use_this_line = true;
  }

  delete gp;    
  delete log10_gp;  
  return use_this_line;
}

bool binomial_del_tmp1(string &line, string & output_line, map <int, EmpiricalPdf *> & pdf_rg){
  float *log10_gp = new float[3];
  stringstream ss, ss_out;
  string chrn, meanCov;
  int aluBegin, aluEnd, midCnt, clipCnt, unknowCnt;
  ss.clear(); ss.str(line); 
  ss >> chrn >> aluBegin >> aluEnd >> meanCov >> midCnt >> clipCnt >> unknowCnt ;
  float prob_ub = pow(10, -LOG10_RATIO_UB);
  float *gp = new float[3];

  int n0 = 0;
  int n1 = 0;
  if (unknowCnt) { 
    int insert_len, idx;
    string token;
    for (int i = 0; i < unknowCnt; i++) {
      getline(ss, token, ':');
      seqan::lexicalCast2(idx, token);
      getline(ss, token, ' ');
      seqan::lexicalCast2(insert_len, token);      
      float p_y = pdf_rg[idx]->pdf_obs(insert_len);
      float p_z = pdf_rg[idx]->pdf_obs(insert_len - aluEnd + aluBegin);
      if (p_y >= p_z * 5) n0 ++;
      else if (p_z >= p_y * 5) n1 ++;
    }
  }

  n0 += midCnt;
  n1 += clipCnt;
  if (n0 + n1 == 0) 
    return false;

  float alpha = 0.9999;

  log10_gp[0] = n0 * log10 (alpha) + n1 * log10 (1 - alpha);
  log10_gp[1] = (n0+n1) * log10(0.5);
  log10_gp[2] = n1 * log10 (alpha) + n0 * log10 (1 - alpha);
  log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);  // normalize such that sum is 1
  cout << "debug1## " << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2] << endl;   
  
  bool use_this_line = false;
  if ( !p00_is_dominant(log10_gp, - LOG10_GENO_PROB) ) {
    ss_out << chrn << " " << aluBegin << " " << aluEnd << " " << midCnt << " " << clipCnt << " " << unknowCnt ;
    log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);  // normalize such that sum is 1
    ss_out << " " << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2]; 
    output_line = ss_out.str();
    use_this_line = true;
  }

  delete gp;    
  delete log10_gp;  
  return use_this_line;
}

void calculate_genoProb(string fn_tmp1, string fn_tmp2, map <int, EmpiricalPdf *> & pdf_rg){
  // clip_read: evidence for deletion, mid_read: evidence for insertion
  ofstream fout(fn_tmp2.c_str());
  fout << "chr aluBegin aluEnd midCnt clipCnt unknowCnt 00 01 11\n";  
  string line, output_line;
  ifstream fin(fn_tmp1.c_str());
  assert(fin);
  getline(fin, line); // read header
  while (getline(fin, line))
    if (parseline_del_tmp1(line, output_line, pdf_rg))
      fout << output_line << endl;
  fin.close();
  fout.close();
}

void read_highCov_region(string f_input, map < string, set<int> > & chrn_aluBegin) {
  ifstream fin(f_input.c_str());
  string tmp1, tmp2, chrn;
  int aluBegin, aluEnd;
  while ( fin >> tmp1>> chrn >> aluBegin >> aluEnd >> tmp2 ) chrn_aluBegin[chrn].insert(aluBegin);
  fin.close();
}

void remove_highCov_region(string f_input, string f_output, int offset, map <string, set<int> > &chrn_aluBegin) {
  string line, chrn;
  int aluBegin;
  stringstream ss;
  ifstream fin(f_input.c_str());
  if(!fin) {
    cerr << f_input << " does not exist!\n";
    exit(0);
  }
  ofstream fout(f_output.c_str());
  getline(fin, line);
  fout << line << endl;
  while (getline(fin, line)) {
    if (line[0]=='#') {
      fout << line << endl;
      continue;
    }
    ss.clear(); ss.str(line); 
    ss >> chrn >> aluBegin;
    aluBegin = aluBegin + offset;
    if (chrn_aluBegin[chrn].find(aluBegin) == chrn_aluBegin[chrn].end())
      fout << line << endl;
  }
  fin.close();
  fout.close();
}

int main( int argc, char* argv[] )
{
  string config_file = argv[1];
  string opt = argv[2];
  if (argc < 3) exit(1);

  boost::timer clocki;    
  clocki.restart();  
  ConfigFileHandler cf_fh = ConfigFileHandler(config_file);
  map<int, string> ID_pn;
  get_pn(cf_fh.get_conf("file_pn"), ID_pn);

  vector<string> chrns;
  for (int i = 1; i < 23; i++)  chrns.push_back("chr" + int_to_string(i) );
  string file_fa_prefix = cf_fh.get_conf("file_fa_prefix");
  string file_dist_prefix = cf_fh.get_conf("file_dist_prefix");

  string path0 = cf_fh.get_conf("file_alu_delete0");
  check_folder_exists(path0);

  if ( opt == "write_tmps_pn" ) { 
    int idx_pn = seqan::lexicalCast<int> (argv[3]);
    assert(argc == 4);
    string pn = ID_pn[idx_pn];
    map<string, int> rg_to_idx;
    parse_reading_group( get_name_rg(file_dist_prefix, pn), rg_to_idx );
    
    unsigned coverage_max = seqan::lexicalCast<unsigned> (cf_fh.get_conf("coverage_max"));
    string file_alupos_prefix = cf_fh.get_conf("file_alupos_prefix"); 
    string fn_tmp1 = path0 + pn + ".tmp1";
    string fn_log1 = path0 + pn + ".log1";
    int minLen_alu_del = seqan::lexicalCast <int> (cf_fh.get_conf("minLen_alu_del"));
    /*
    string bam_input = cf_fh.get_conf("file_bam_prefix") + pn + ".bam";
    string bai_input = bam_input + ".bai";      
    BamFileHandler *bam_fh = BamFileHandler::openBam_24chr(bam_input, bai_input);
    delete_search(minLen_alu_del, bam_fh, file_fa_prefix, chrns, fn_tmp1, fn_log1, file_alupos_prefix, coverage_max, rg_to_idx);
    delete bam_fh;
    move_files(path0+"log1s/", path0 + pn + ".log1") ;
    */
    string fn_tmp2 = path0 + pn + ".tmp2";
    map <int, EmpiricalPdf *> pdf_rg;    
    string pdf_param = cf_fh.get_conf("pdf_param"); // 100_1000_5  
    read_pdf_pn(file_dist_prefix, pn, pdf_param, pdf_rg);
    calculate_genoProb(fn_tmp1, fn_tmp2, pdf_rg); 
    EmpiricalPdf::delete_map(pdf_rg);
    //move_files(path0+"tmp1s/", path0 + pn + ".tmp1") ;
    //move_files(path0+"tmp2s/", path0 + pn + ".tmp2") ;
    
  } else if (opt == "write_vcf_pns") {   // write vcf for all pn
    vector <string> pns;
    read_file_pn_used(cf_fh.get_conf("pn_del_vcf"), pns);  // select some pns for writing vcf files 
    string path_input = path0 + "tmp2s/";
    string fn_pos, fn_vcf;
    fn_pos = path0 + int_to_string( pns.size()) + ".pos";
    filter_by_llh_noPrivate(path_input, ".tmp2", fn_pos + ".tmp", pns, chrns, 7);
    fn_vcf = path0 + int_to_string( pns.size()) + ".vcf";  
    combine_pns_vcf_noPrivate(path_input, ".tmp2", fn_vcf + ".tmp", pns, chrns, 7);  
    
    map <string, set<int> > chrn_aluBegin;
    for ( vector <string>::iterator pi = pns.begin(); pi != pns.end(); pi++) {
      string fn_log1 = path0 + "log1s/" + *pi + ".log1";  
      read_highCov_region(fn_log1, chrn_aluBegin);
    }
    remove_highCov_region(fn_pos+".tmp", fn_pos, 0, chrn_aluBegin);
    remove_highCov_region(fn_vcf+".tmp", fn_vcf, -1, chrn_aluBegin);
    system( ("rm " + fn_pos+".tmp").c_str() );    
    system( ("rm " + fn_vcf+".tmp").c_str() );    
    cout << "regions with high coverage are removed\n";

  } else if (opt == "debug1") {
    float *log10_gp = new float[3];
    float *gp = new float[3];
    float offset = 0.4;
    log10_gp[0] = -4.168 + offset;
    log10_gp[1] = -0.60814 + offset;
    log10_gp[2] = -23.9364 + offset;
    log10P_to_P(log10_gp, gp, LOG10_GENO_PROB);
    cout << setprecision(6) << gp[0] << " " << gp[1] << " " << gp[2]; 
    delete log10_gp;
    delete gp;
    cout << "test1 done\n";

  } else if (opt == "debug2") {

    string pn = argv[3];
    string line, output_line;
    map <int, EmpiricalPdf *> pdf_rg;    
    string pdf_param = cf_fh.get_conf("pdf_param"); // 100_1000_5  
    read_pdf_pn(file_dist_prefix, pn, pdf_param, pdf_rg);

    cout << "chr aluBegin aluEnd midCnt clipCnt unknowCnt 00 01 11\n";    
    //line = "chr21 21903875 21904163 26 48 5 2 0:525 0:534";
    line = "chr21 41931103 41931403 30 62 6 2 0:514 0:550";

    cout << line << endl;
    //parseline_del_tmp1(line, output_line, pdf_rg);
    binomial_del_tmp1(line, output_line, pdf_rg);
    
    cout << output_line << endl;
    
  } else if (opt == "debug3") {  // check some region 

    string pn = argv[3];

    string chrn = "chr3";
    int aluBegin = 129912695;
    int aluEnd = 129913004;
    map < seqan::CharString, T_READ> qName_info;  
    
    FastaFileHandler *fasta_fh = new FastaFileHandler(file_fa_prefix + chrn + ".fa", chrn);    
    string bam_input = cf_fh.get_conf("file_bam_prefix") + pn + ".bam";
    string bai_input = bam_input + ".bai";      
    BamFileHandler *bam_fh = BamFileHandler::openBam_24chr(bam_input, bai_input);
    seqan::BamAlignmentRecord record;  

    bam_fh->jump_to_region(chrn, aluBegin-ALU_FLANK, aluEnd + ALU_FLANK);
    while ( true ) {
      string read_status = bam_fh->fetch_a_read(chrn, aluBegin - ALU_FLANK, aluEnd + ALU_FLANK, record);
      if (read_status == "stop" ) break;
      if (read_status == "skip" or !QC_delete_read(record)) continue;
      map <seqan::CharString, T_READ >::iterator qItr = qName_info.find(record.qName);
      if ( qItr != qName_info.end() and qItr->second != unknow_read) 
	continue;
      T_READ iread = classify_read( record, aluBegin, aluEnd, fasta_fh);    
      if ( iread == useless_read) continue;    
      qName_info[record.qName] = iread;
    }
    
    map <seqan::CharString, int> unknow_reads;
    for (map < seqan::CharString, T_READ>::iterator qi = qName_info.begin(); qi != qName_info.end(); qi ++ ) {
      if ( qi->second == unknow_read) {
	unknow_reads[qi->first] = 1;
      }
    }    
    bam_fh->jump_to_region(chrn, aluBegin-ALU_FLANK, aluEnd + ALU_FLANK);
    while ( true ) {
      string read_status = bam_fh->fetch_a_read(chrn, aluBegin - ALU_FLANK, aluEnd + ALU_FLANK, record);
      if (read_status == "stop" ) break;
      if (read_status == "skip" or !QC_delete_read(record)) continue;
    }    
    delete bam_fh;    
  } else {
    cout << "unknown options !\n";

  }
  
  cout << "time used " << clocki.elapsed() << endl;
  return 0;  
}
