#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <string.h>
#include <bitset>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <iostream>
#include <string>

//Common helper classes
//Setup to allow TraceUID to be used as hashmap key
struct TraceUID
{
  uint64_t pc;
  uint64_t n_visited;

  TraceUID(){}
  TraceUID(uint64_t pc, uint64_t n_visited): pc(pc), n_visited(n_visited){}

  bool operator==(const TraceUID &other) const
    {
      return (pc == other.pc && n_visited == other.n_visited);
    }

  bool operator<(const TraceUID &other) const
  {
    return pc == other.pc ? n_visited < other.n_visited : pc < other.pc;
  }
};

  template<>
  struct std::hash<TraceUID>
  {
    std::size_t operator()(const TraceUID& t) const noexcept
    {
      std::size_t h1 = std::hash<uint64_t>{}(t.pc);
      std::size_t h2 = std::hash<uint64_t>{}(t.n_visited);
      return h1 ^ (h2 << 1);
    }
  };

  namespace std {
    string to_string(const TraceUID &t);
  }


struct trace_elem
{
    bool valid;
    bool load;
    TraceUID tuid;
    TraceUID dep;
    uint64_t seqNum;
    uint64_t effSeqNum;
    uint64_t mem_addr;
    uint32_t mem_size;

  trace_elem(){}
  trace_elem( 
    bool _valid,
    bool _load,
    TraceUID _tuid,
    TraceUID _dep,
    uint64_t _seqNum,
    uint64_t _effSeqNum,
    uint64_t _mem_addr,
    uint64_t _mem_size): 
    valid(_valid), load(_load), tuid(_tuid), dep(_dep), seqNum(_seqNum), 
    effSeqNum(_effSeqNum), mem_addr(_mem_addr), mem_size(_mem_size){}
};

  //Tuple holds bool:  Valid (bool), Load(true)/store(false),
  //this pc/access_number, pc/access_number dependent, sequence_number,
  //eff_sequence_number, mem_addr, access size (Bytes)

// using full_trace_T = std::tuple<bool, bool, TraceUID, TraceUID, uint64_t,
//       uint64_t, uint64_t>;

std::vector<trace_elem> trace;

  void load_trace(boost::iostreams::filtering_istream& infile, uint64_t n){
      std::string line;

      if (infile.fail()){
          printf("Failed to open mem trace path \n");
          return;
      }

      uint64_t line_n = 0;
      while (std::getline(infile, line) && line_n < n)
      {
          //Allow for comments to be ignored
          if (line[0] == '#'){
              continue;
          }

          bool valid;
          bool load;
          TraceUID tuid;
          TraceUID dep;
          uint64_t seqNum;
          uint64_t effSeqNum;
          uint64_t mem_addr;
          uint64_t mem_size;

          uint64_t pc,n_visited;

          std::string temp;
          std::stringstream ss(line);

          std::getline(ss, temp, ',');
          valid = std::stoull(temp);

          std::getline(ss, temp, ',');
          load = std::stoull(temp);

          std::getline(ss, temp, ':');
          pc = std::stoull(temp);

          std::getline(ss, temp, ',');
          n_visited = std::stoull(temp);

          tuid = TraceUID(pc, n_visited);

          std::getline(ss, temp, ':');
          pc = std::stoull(temp);

          std::getline(ss, temp, ',');
          n_visited = std::stoull(temp);

          dep = TraceUID(pc, n_visited);

          std::getline(ss, temp, ',');
          seqNum = std::stoull(temp);

          std::getline(ss, temp, ',');
          effSeqNum = std::stoull(temp);

          std::getline(ss, temp, ',');
          mem_addr = std::stoull(temp);

          //This is bugged rn, the trace does not get serialised properly
          // std::getline(ss, temp, ',');
          // mem_size = std::stoull(temp);
          mem_size = 1;

          trace.push_back(
              trace_elem(valid, load, tuid, dep, seqNum, effSeqNum, mem_addr, mem_size));

          line_n++;
      }
  };

  //Address to seqnum distance
  std::map<uint64_t, double> seqDist;
  std::unordered_map<uint64_t, uint64_t> seqDistCache;

  //Address to effective seqnum distance
  std::map<uint64_t, double> effSeqDist;
  std::unordered_map<uint64_t, uint64_t> effSeqDistCache;

  //Address to effective seqnum distance
  uint64_t global_branch_n;
  std::map<uint64_t, double> branchDist;
  std::unordered_map<uint64_t, uint64_t> branchDistCache;

  //Pc -> Pc + count relation
  //Backwards address cache address -> PC
  std::unordered_map<uint64_t, uint64_t> pairCache;
  std::unordered_map<uint64_t, std::unordered_map<uint64_t,double>> pairCounts;

  //Path count
  const u_int32_t branch_hist_size = 1024;
  std::bitset<branch_hist_size> global_branch_hist = 0;
  std::unordered_map<uint64_t, std::unordered_map<std::bitset<branch_hist_size> ,double>> pairCounts;

  class MDP_path {
    

  }

  void traverse(float weight, uint64_t warmup){

      for(auto i = trace.begin(); i != trace.end(); i++){

          trace_elem elem = *i;

          //Normal store
          if(!elem.load){
              for (uint32_t i = 0; i < elem.mem_size; i++){
                seqDistCache[elem.mem_addr + i] = elem.seqNum;
                effSeqDistCache[elem.mem_addr + i] = elem.effSeqNum;
                branchDistCache[elem.mem_addr + i] = global_branch_n; 
                pairCache[elem.mem_addr + i] = elem.tuid.pc;
              }

          //Normal load
          } else if(elem.load && elem.valid && elem.effSeqNum > warmup){
                
                //Log seq distance 
                std::set<uint64_t> seq_dist_set;

                for (uint32_t i = 0; i < elem.mem_size; i++){
                  if (seqDistCache.contains(elem.mem_addr + i))
                      seq_dist_set.insert(elem.seqNum - seqDistCache[elem.mem_addr + i]);
                  else 
                      seq_dist_set.insert(0);
                }

                for (auto seq_dist_elem : seq_dist_set){
                    if (seqDist.contains(seq_dist_elem))
                        seqDist[seq_dist_elem] += weight;
                    else
                        seqDist[seq_dist_elem] = weight;
                }

                //Log eff seq distance 
                std::set<uint64_t> eff_seq_dist_set;

                for (uint32_t i = 0; i < elem.mem_size; i++){
                  if (effSeqDistCache.contains(elem.mem_addr + i))
                      eff_seq_dist_set.insert(elem.effSeqNum - effSeqDistCache[elem.mem_addr + i]);
                  else 
                      eff_seq_dist_set.insert(0);
                }

                for (auto eff_seq_dist_elem : eff_seq_dist_set){
                    if (effSeqDist.contains(eff_seq_dist_elem))
                        effSeqDist[eff_seq_dist_elem] += weight;
                    else
                        effSeqDist[eff_seq_dist_elem] = weight;
                }

                //Log branch distance
                std::set<uint64_t> branch_dist_set;
                for (uint32_t i = 0; i < elem.mem_size; i++){
                                                   
                    if (branchDistCache.contains(elem.mem_addr + i))
                        branch_dist_set.insert(global_branch_n - branchDistCache[elem.mem_addr + i]);
                    else 
                        branch_dist_set.insert(uint64_t(-1));
                }

                for (auto branch_dist_elem : branch_dist_set){
                  if (branchDist.contains(branch_dist_elem))
                      branchDist[branch_dist_elem] += weight;
                  else
                      branchDist[branch_dist_elem] = weight;

                }
 
                // Log cache pairs
                std::set<uint64_t> pc_set;
                for (uint32_t i = 0; i < elem.mem_size; i++){
                  if (pairCache.contains(elem.mem_addr + i))
                      pc_set.insert(pairCache[elem.mem_addr + i]);
                  else
                      pc_set.insert(1);
                }
                
                for (auto pc_set_elem : pc_set){
                  if (pairCounts[elem.tuid.pc].contains(pc_set_elem))
                      pairCounts[elem.tuid.pc][pc_set_elem] += weight;
                  else
                      pairCounts[elem.tuid.pc][pc_set_elem] = weight;
                }

          //Branch 
          } else if (elem.load && !elem.valid){
            global_branch_n++;

          } 
      }
  }

//Please don't mangle my symbols so that I can call you from python 👉👈
extern "C" {
  char* get_seq_dists(){
    std::string out = "";

    for ( auto dist : seqDist){
        out += std::to_string(dist.first);
        out +=':';
        out += std::to_string(dist.second);
        out +=',';
    }

    return strdup(out.c_str());
  }

  char* get_eff_seq_dists(){
    std::string out = "";

    for ( auto dist : effSeqDist){
        out += std::to_string(dist.first);
        out +=':';
        out += std::to_string(dist.second);
        out +=',';
    }

    return strdup(out.c_str());
  }

  char* get_branch_dists(){
    std::string out = "";

    for ( auto dist : branchDist){
            out += std::to_string(dist.first);
            out +=':';
            out += std::to_string(dist.second);
            out +=',';
    }

    return strdup(out.c_str());
  }

  char* get_takenness(){
    //Initialize histogram
    const uint32_t histogram_bins = 40;
    std::vector<float> histogram(histogram_bins, 0);

    //Iterate over outer map
    for(auto outer : pairCounts){

        //Calculate sum of inner map
        double inner_sum = 0;
        for(auto inner : outer.second){
            inner_sum += inner.second;
        }

        //Add takenness to histogram
        for(auto inner : outer.second){
            float takenness = float(inner.second) / float(inner_sum);

            int hist_idx = takenness * histogram_bins;
            histogram[hist_idx] += inner.second;
        }
    }

    //Dump histogram to string
    std::string out = "";

    for ( auto h : histogram){
            out += std::to_string(h);
            out +=',';
    }

    return strdup(out.c_str());
  }

  void clear_all(){
    trace.clear();

    //Address to seqnum distance
    seqDist.clear();
    seqDistCache.clear();

    //Address to effective seqnum distance
    effSeqDist.clear();
    effSeqDistCache.clear();

    //Address to effective seqnum distance
    global_branch_n = 0;
    branchDist.clear();
    branchDistCache.clear();

    //Pc -> Pc + count relation
    pairCache.clear();
    pairCounts.clear();

  }

  void clear_caches(){
    trace.clear();

    //Address to seqnum distance
    seqDistCache.clear();

    //Address to effective seqnum distance
    effSeqDistCache.clear();

    //Address to effective seqnum distance
    global_branch_n = 0;
    branchDistCache.clear();

    //Pc -> Pc + count relation
    pairCache.clear();

  }

  void calculate_statistics(char const* trace_path, float weight, uint32_t warmup){
      boost::iostreams::filtering_istream infile;
      infile.push(boost::iostreams::zstd_decompressor());
      infile.push(boost::iostreams::file_source(trace_path));

    //Chunk loading and processing
    //while (infile.peek() != EOF){
    while (!infile.eof()){
        //printf("Back to loading\n");
        load_trace(infile, 1000000);
        //printf("Back to traversing\n");
        traverse(weight, warmup);
        trace.clear();
    }
  }
}

int main(){
    //Load trace from file
    printf("Running main\n");
    clear_all();
    clear_caches();
    calculate_statistics("/home/michal/Desktop/windows/FYP/spec_2017_rate_trace/557.xz_r/0/0/full_trace.csv.zst", 0.153887, 1000000);
    clear_caches();
    calculate_statistics("/home/michal/Desktop/windows/FYP/spec_2017_rate_trace/557.xz_r/0/1/full_trace.csv.zst", 0.00309807, 1000000);
    printf("%s", get_takenness());
}