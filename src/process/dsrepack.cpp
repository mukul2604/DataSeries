// -*-C++-*-
/*
  (c) Copyright 2003-2007, Hewlett-Packard Development Company, LP

  See the file named COPYING for license details
*/

/** @file
    Select subset of fields from a collection of traces, generate a new trace
*/

/* 
   =pod

   =head1 NAME

   dsrepack - split or merge DataSeries files and change the compression types

   =head1 SYNOPSIS

   dsrepack [common-options] [--verbose] [--target-file-size=MiB] [--no-info] 
   input-filename... output-filename

   =head1 DESCRIPTION

   In the simplest case, dsrepack takes the input files, and copies them
   to the output filename changing the extent size and compression level
   as specified in the common options.  If max-file-size is set,
   output-filename is used as a base name, and the actual output names
   will be output-filename.####.ds, starting from 0.

   =head1 EXAMPLES

   dsrepack --extent-size=131072 --compress none --enable lzo lz4 cello97*ds all-cello97.ds

   dsrepack --extent-size=67108864 --compress bz2 --target-file-size=100 \
   nettrace.000000-000499.ds -- nettrace.000000-000499.split


   =head1 OPTIONS
   
   All of the common options apply, see dataseries(7).

   =over 4

   =item B<--target-file-size=MiB>

   What is the target file size to be generated.  Each of the individual
   files will be about the target file size, but the vagaries of
   compression mean that the files will vary around the target file size.
   The last file may be arbitrarily small.  MiB can be specified as a
   double, so one could say 0.1 MiB, but it's not clear if that is
   useful.

   =item B<--no-info>

   Do not generate the Info::DSRepack extent.  This means that the output files
   will contain exactly the same information as the source files.  Normally the
   info extent is generated so the compression options for a file are known.

   This cannot be used when repacking a trace that already contains the 
   Info::DSRepack extent, as dsrepack does not support removing trace data.

   =item B<--verbose, -v>

   Outputs progress reports as it processes the extents.

   =back

   =head1 SEE ALSO

   dataseries-utils(7), dsselect(1)

   =cut
*/

// TODO: use GeneralField/ExtentRecordCopy, we aren't changing the type so
// it should be much faster.

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/format.hpp>

#include <Lintel/AssertBoost.hpp>
#include <Lintel/LintelLog.hpp>
#include <Lintel/StringUtil.hpp>
#include <Lintel/PointerUtil.hpp>

#include <DataSeries/commonargs.hpp>
#include <DataSeries/DataSeriesFile.hpp>
#include <DataSeries/GeneralField.hpp>
#include <DataSeries/DataSeriesModule.hpp>
#include <DataSeries/TypeIndexModule.hpp>
#include <DataSeries/PrefetchBufferModule.hpp>

static const bool debug = false;
static bool show_progress = false;
static bool generate_info_extent = true;

using namespace std;
using lintel::safeDownCast;

// TODO: figure out why when compressing from lzo to lzf, we can only
// get up to ~200% cpu utilization rather than going to 400% on a 4way
// machine.  Possibilites are decompression bandwidth and copy
// bandwidth, but neither seem particularly likely.  One oddness from
// watching the strace is that the source file seems to be very bursty
// rather than smoothly reading in data.  Watching the threads in top
// on a 2 way machine, we only got to ~150% cpu utilization yet no
// thread was at 100%.  Fiddling with priorities of the compression
// threads in DataSeriesFile didn't seem to do anything.

struct PerTypeWork {
    OutputModule *output_module;
    ExtentSeries inputseries, outputseries;
    // Splitting out the types here is ugly.  However, it's a huge
    // performance improvement to not be going through the virtual
    // function call.  In particular, repacking an BlockIO::SRT file
    // from LZO to LZF took 285 CPU seconds originally, 244 CPU s
    // after adding in the bool specific case, and 235 seconds after
    // special casing for int32's.  That file has lots of booleans
    // somewhat fewer int32s, and then some doubles.

    // TODO: remove special case, use ExtentRecordCopy
    vector<GeneralField *> infields, outfields, all_fields;
    vector<GF_Bool *> in_boolfields, out_boolfields;
    vector<GF_Int32 *> in_int32fields, out_int32fields;
    vector<GF_Variable32 *> in_var32fields, out_var32fields;

    double sum_unpacked_size, sum_packed_size;
    PerTypeWork(DataSeriesSink &output, unsigned extent_size, 
                const ExtentType::Ptr t) 
            : inputseries(t), outputseries(t), sum_unpacked_size(0), sum_packed_size(0) 
    {
        for (unsigned i = 0; i < t->getNFields(); ++i) {
            const string &s = t->getFieldName(i);
            GeneralField *in = GeneralField::create(NULL, inputseries, s);
            GeneralField *out = GeneralField::create(NULL, outputseries, s);
            all_fields.push_back(in);
            all_fields.push_back(out);
            switch(t->getFieldType(s)) 
            {
                case ExtentType::ft_bool: 
                    in_boolfields.push_back(safeDownCast<GF_Bool>(in));
                    out_boolfields.push_back(safeDownCast<GF_Bool>(out));
                    break;
                case ExtentType::ft_int32: 
                    in_int32fields.push_back(safeDownCast<GF_Int32>(in));
                    out_int32fields.push_back(safeDownCast<GF_Int32>(out));
                    break;
                case ExtentType::ft_variable32: 
                    in_var32fields.push_back(safeDownCast<GF_Variable32>(in));
                    out_var32fields.push_back(safeDownCast<GF_Variable32>(out));
                    break;
                default:
                    infields.push_back(in);
                    outfields.push_back(out);
                    break;
            }
        }
        output_module = new OutputModule(output, outputseries, t, 
                                         extent_size);
    }

    ~PerTypeWork() {
        for (vector<GeneralField *>::iterator i = all_fields.begin();
            i != all_fields.end(); ++i) {
            delete *i;
        }
    }

    void rotateOutput(DataSeriesSink &output) {
        OutputModule *old = output_module;
        old->close();
        
        INVARIANT(!outputseries.hasExtent(), "huh?");

        DataSeriesSink::Stats stats = old->getStats();
        sum_unpacked_size += stats.unpacked_size;
        sum_packed_size += stats.packed_size;

        output_module = new OutputModule(output, outputseries, old->getOutputType(),
                                         old->getTargetExtentSize());
        delete old;
    }

    uint64_t estimateCurSize() {
        // Assume 3:1 compression if we have no statistics
        double ratio = sum_unpacked_size > 0 ? sum_packed_size / sum_unpacked_size : 0.3;
        return static_cast<uint64_t>(output_module->curExtentSize() * ratio);
    }
};

uint64_t fileSize(const string &filename) {
    struct stat buf;
    INVARIANT(stat(filename.c_str(),&buf) == 0, boost::format("stat(%s) failed: %s")
              % filename % strerror(errno));

     BOOST_STATIC_ASSERT(sizeof(buf.st_size) >= 8);
     return buf.st_size;
}

void checkFileMissing(const std::string &filename) {
    struct stat buf;
    INVARIANT(stat(filename.c_str(), &buf) != 0, 
              boost::format("Refusing to overwrite existing file %s.\n")
              % filename);
}


const string generate_dsrepack_info_type_xml() {

    string ret = "<ExtentType namespace=\"ssd.hpl.hp.com\" name=\"Info::DSRepack\" version=\"1.0\" >\n";

    // Skip i = 0, which is compress none
    for (int i = 1; i < Extent::num_comp_algs; ++i ) {
        string alg_name = Extent::compression_algs[i].name;
        ret += "  <field type=\"bool\" name=\"enable_" + alg_name +
               "\" print_true=\"with_" + alg_name + "\" print_false=\"no_" +
               alg_name + "\" />\n";
    }
    
    ret += "  <field type=\"int32\" name=\"compress_level\" />\n"
           "  <field type=\"int32\" name=\"extent_size\" />\n"
           "  <field type=\"int32\" name=\"part\" opt_nullable=\"yes\" />\n"
           "</ExtentType>\n";

    return ret;
}

const string dsrepack_info_type_xml(generate_dsrepack_info_type_xml());

ExtentType::Ptr dsrepack_info_type;

void writeRepackInfo(DataSeriesSink &sink, const commonPackingArgs &cpa, int file_count) {
    if (!generate_info_extent) {
        return;
    }
    ExtentSeries series(dsrepack_info_type);
    OutputModule out_module(sink, series, dsrepack_info_type, cpa.extent_size);

    /* Previously, this line occured between initializing all the fields and
       calling set() on them.  Having it here doesn't seem to cause any problems,
       but be aware that there may be some hidden conflicts.
       -- DYS and ASG July 2013 */
    out_module.newRecord();

    Int32Field compress_level(series, "compress_level");
    Int32Field extent_size(series, "extent_size");
    Int32Field part(series, "part", Field::flag_nullable);
    
    // Skip i = 0, which is compress none
    for (int i = 1; i < Extent::num_comp_algs; ++i ) {
        string enableStr = "enable_" + (string)Extent::compression_algs[i].name;
        BoolField next_field(series, enableStr);
        next_field.set(cpa.compress_modes & 
                       Extent::compression_algs[i].compress_flag ?
                       true : false );
    }

    compress_level.set(cpa.compress_level);
    extent_size.set(cpa.extent_size);
    if (file_count >= 0) {
        part.set(file_count);
    } else {
        part.setNull();
    }
}

bool skipType(const ExtentType::Ptr type) {
    return type->getName() == "DataSeries: ExtentIndex"
            || type->getName() == "DataSeries: XmlType"
            || (type->getName() == "Info::DSRepack"
                && type->getNamespace() == "ssd.hpl.hp.com");
}

void usage(const string argv0, const string &error) {
    FATAL_ERROR(boost::format("Error:%s\nUsage: %s [common-args] [--target-file-size=MiB] input-filename... output-filename\nCommon args:\n%s") 
                % error % argv0 % packingOptions());
}

// TODO: Split up main(), it's getting a bit large
const string target_file_size_arg("--target-file-size=");

int main(int argc, char *argv[]) {
    
#ifdef __linux__
    // These times may not be completely accurate; they should however be precise
    // enough to use when comparing repacking times relative to others.
    clock_t start_time = clock();   
    timespec* start_clock = new timespec;
    clock_gettime(CLOCK_REALTIME, start_clock);
#endif

    LintelLog::parseEnv();
    uint64_t target_file_bytes = 0;
    commonPackingArgs packing_args;
    getPackingArgs(&argc,argv,&packing_args);

    while (argc > 1 && argv[1][0] == '-') {
        if (prefixequal(argv[1], target_file_size_arg)) {
            double mib = stringToDouble(string(argv[1]).substr(target_file_size_arg.size()));
            INVARIANT(mib > 0, "max file size MiB must be > 0");
            target_file_bytes = static_cast<uint64_t>(mib * 1024.0 * 1024.0);
        } else if (string(argv[1]) == "--no-info") {
            generate_info_extent = false;
        } else if (string(argv[1]) == "--verbose" || string(argv[1]) == "-v") {
            show_progress = true;
        } else {
            usage(argv[0], 
                  (boost::format("unknown argument '%s'") % argv[1]).str());
        }
        for (int i = 2; i < argc; ++i) {
            argv[i-1] = argv[i];
        }
        --argc;
    }

    if (argc <= 2) {
        usage(argv[0], "expected source and destination files");
    }

    // Always check on repacking...
    if (getenv("DATASERIES_EXTENT_CHECKS")) {
        cerr << "Warning: DATASERIES_EXTENT_CHECKS is set; generally you want all checks on during a dsrepack.\n";
    }
    Extent::setReadChecksFromEnv(true);

    TypeIndexModule source(""); 
    ExtentTypeLibrary library;
    if (generate_info_extent) {
        dsrepack_info_type = library.registerTypePtr(dsrepack_info_type_xml);
    }
    map<string, PerTypeWork *> per_type_work;

    string output_base_path(argv[argc-1]);
    unsigned output_file_count = 0;
    string output_path;
    if (target_file_bytes == 0) {
        output_path = output_base_path;
    } else {        
        // %02d -- possible but unlikely that we would write over 100
        // split files, but seems sufficiently unlikely that it's not
        // worth having three digits of split numbers always.  Common
        // case likely to be below 10, but splitting ~1G into 100MB
        // chunks could end up with more than 10, so want two digits.
        
        // Bumped up to 4 digits, since the cost of having extra digits in your
        // file names is negligible, especialy compared to the potential
        // issues of running out of file names.
        // -- DYS and ASB July 2013
        output_path = (boost::format("%s.part-%04d.ds") 
                       % output_base_path % output_file_count).str();
    }
    checkFileMissing(output_path);
    DataSeriesSink *output = 
            new DataSeriesSink(output_path, packing_args.compress_modes,
                               packing_args.compress_level);

    uint32_t extent_count = 0;
    for (int i = 1; i < (argc-1); ++i) {
        source.addSource(argv[i]);

        // Nothing helping the fact that we have to open all of the
        // files to verify type identicalness before we can re-pack
        // things.  Luckily people should only end up doing this
        // infrequently when they've just retrieved bz2 compressed
        // extents and want to make them larger and faster, or to do
        // the reverse for distribution.

        DataSeriesSource f(argv[i]);

        for (ExtentTypeLibrary::NameToType::iterator j = f.getLibrary().name_to_type.begin();
             j != f.getLibrary().name_to_type.end(); ++j) {
            if (skipType(j->second)) {
                continue;
            }
            if (prefixequal(j->first, "DataSeries:")) {
                cerr << boost::format("Warning, found extent type of name '%s'; probably should skip it") % j->first << endl;
            }
            const ExtentType::Ptr tmp = library.getTypeByNamePtr(j->first, true);
            INVARIANT(tmp == NULL || tmp == j->second,
                      boost::format("XML types for type '%s' differ between file %s and an earlier file")
                      % j->first % argv[i]);
            if (tmp == NULL) {
                if (debug) {
                    cout << "Registering type of name " << j->first << endl;
                }
                const ExtentType::Ptr t
                        = library.registerTypePtr(j->second->getXmlDescriptionString());
                per_type_work[j->first] = 
                        new PerTypeWork(*output, packing_args.extent_size, t);
            }
            DEBUG_INVARIANT(per_type_work[j->first] != NULL, "internal");
        }

        ExtentSeries s(f.index_extent);
        Variable32Field extenttype(s,"extenttype");

        for (; s.morerecords(); ++s) {
            if (skipType(library.getTypeByNamePtr(extenttype.stringval()))) {
                continue;
            }
            ++extent_count;
        }
    }

    DataSeriesModule *from = &source;   

    // TODO: look at the number of cores we have and set these values
    // more appropriately based on that, in particular, we want
    // maxBytesInProgress =~ (ncpus+1) * output-extent-size * 2
    // Make some assumption along the lines of 0.5-1GB of memory/core
    source.startPrefetching(32*1024*1024, 224*1024*1024); // 256MiB total
    // want a fair bit here in case we are writing big extents since 
    // during compression they use 2x the size.
    output->setMaxBytesInProgress(512*1024*1024); 
    output->writeExtentLibrary(library);

    DataSeriesSink::Stats all_stats;
    uint32_t extent_num = 0;
    uint64_t cur_file_bytes = 0;

    while (true) {
        Extent::Ptr inextent = from->getSharedExtent();
        if (inextent == NULL)
            break;
        
        if (skipType(inextent->type)) {
            continue;
        }

        ++extent_num;

        if (show_progress) {
            cout << boost::format("Processing extent #%d/%d of type %s\n")
                    % extent_num % extent_count % inextent->type->getName();
        }
        PerTypeWork *ptw = per_type_work[inextent->type->getName()];
        INVARIANT(ptw != NULL, "internal");
        for (ptw->inputseries.setExtent(inextent);
             ptw->inputseries.morerecords();
             ++ptw->inputseries) {
            ptw->output_module->newRecord();
            cur_file_bytes += ptw->outputseries.getTypePtr()->fixedrecordsize();
            for (unsigned int i=0; i < ptw->in_boolfields.size(); ++i) {
                ptw->out_boolfields[i]->set(ptw->in_boolfields[i]);
            }
            for (unsigned int i=0; i < ptw->in_int32fields.size(); ++i) {
                ptw->out_int32fields[i]->set(ptw->in_int32fields[i]);
            }
            for (unsigned int i=0; i < ptw->in_var32fields.size(); ++i) {
                cur_file_bytes += ptw->in_var32fields[i]->myfield.size();
                ptw->out_var32fields[i]->set(ptw->in_var32fields[i]);
            }
            for (unsigned int i=0; i<ptw->infields.size(); ++i) {
                ptw->outfields[i]->set(ptw->infields[i]);
            }
            if (target_file_bytes > 0 && cur_file_bytes >= target_file_bytes) {
                output->flushPending();
                uint64_t est_file_size = fileSize(output_path);
                for (map<string, PerTypeWork *>::iterator i = per_type_work.begin();
                     i != per_type_work.end(); ++i) {
                    est_file_size += i->second->estimateCurSize();
                }
                if (est_file_size >= target_file_bytes) {
                    ++output_file_count;

                    INVARIANT(output_file_count < 10000, 
                              "split into >= 10000 parts; assuming you didn't want that and stopping");
                    output_path = (boost::format("%s.part-%02d.ds") 
                                   % output_base_path 
                                   % output_file_count).str();
                    checkFileMissing(output_path);
                    DataSeriesSink *new_output = 
                            new DataSeriesSink(output_path, 
                                               packing_args.compress_modes,
                                               packing_args.compress_level);
                    new_output->writeExtentLibrary(library);
                   
                    for (map<string, PerTypeWork *>::iterator i = per_type_work.begin();
                         i != per_type_work.end(); ++i) {
                        i->second->rotateOutput(*new_output);
                    }
                    writeRepackInfo(*output, packing_args, output_file_count);
                    output->close();
                    all_stats += output->getStats();
                    delete output;
                    output = new_output;
                }
                cur_file_bytes = est_file_size;
            }
        }
    }

    for (map<string, PerTypeWork *>::iterator i = per_type_work.begin();
         i != per_type_work.end(); ++i) {
        i->second->output_module->flushExtent();
    }
    writeRepackInfo(*output, packing_args, target_file_bytes > 0 
                    ? static_cast<int>(output_file_count) : -1);
    
    all_stats += output->getStats();
    output->close();
    
    cout << boost::format("expanded to %d bytes\n") % source.total_uncompressed_bytes;
    
    all_stats.printText(cout);

#ifdef __linux__
    clock_t cpu_time = clock() - start_time;
    timespec* end_clock = new timespec;
    clock_gettime(CLOCK_REALTIME, end_clock);
    double clock_time = (end_clock->tv_sec - start_clock->tv_sec) +
                        ((end_clock->tv_nsec - start_clock->tv_nsec)/1000000000.0);    
    delete start_clock;
    delete end_clock;
    
    cout << boost::format("total repacking clock time: %f") % clock_time << endl;
    cout << boost::format("total repacking cpu time: %f") % ( ((float)cpu_time)/CLOCKS_PER_SEC ) << endl;
#endif

     return 0;
}

