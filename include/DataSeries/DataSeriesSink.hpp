// -*-C++-*-
/*
   (c) Copyright 2003-2011, Hewlett-Packard Development Company, LP

   See the file named COPYING for license details
*/

/** @file
    Class for writing DataSeries files.
*/

#ifndef DATASERIES_SINK_HPP
#define DATASERIES_SINK_HPP

#include <Lintel/Deque.hpp>
#include <Lintel/PThread.hpp>

#include <DataSeries/Extent.hpp>
#include <DataSeries/ExtentField.hpp>

/** \brief Writes Extents to a DataSeries file.
  */
class DataSeriesSink {
public:
    // sundry statistics on this sink; expect to get one
    // non-compressed chunk (the fixed record pointers for the extent
    // type information); also, empty variable-sized extents are not
    // counted in the compress_* stats
    class Stats {
    public:
	/** Single global mutex for stats right now, should be sufficient
	    given they are pretty infrequently accessed. */
	static PThreadMutex &getMutex();

	Stats(const Stats &from) {
	    *this = from;
	}
	Stats & operator=(const Stats &from);
	// If anything other than plain old data gets put in here, 
	// then the assignment operator needs to be fixed.
        /** The total number of Extents written */
	uint32_t extents;
	uint32_t
            /** The number of Extents that were not compressed at all */
            compress_none,
            /** The number of Extents that were compressed using lzo */
            compress_lzo,
            /** The number of Extents that were compressed using gzip */
            compress_gzip, 
            /** The number of Extents that were compressed using bzip2 */
	    compress_bz2,
            /** The number of Extents that were compressed using lzf */
            compress_lzf;
	uint64_t
            /** The total number of bytes in the Extents before compression. */
            unpacked_size,
            /** The number of bytes in fixed size records before
                compression. */
            unpacked_fixed,
            /** The number of bytes in the Extents' string pools
                before compression. */
            unpacked_variable,
            /** The number of bytes in the Extents' string pools
                before any processing.  In particular it will
                include duplicate fields marked with pack_unique. */
	    unpacked_variable_raw,
            /** The total number of bytes after compression. */
            packed_size,
	    /** The number of records that were written */
	    nrecords;

	/** The time spent packing/compressing Extents.  This is
            supposed to be the thread time, rather than wall clock time.
            The pack_time statistic has been disabled pending figuring
	    out how to make it accurate.  For mor discussion, see
	    DataSeriesFile.C:get_thread_cputime() */
	double pack_time; 

        /** Initializes all statistics to 0. */
	Stats() {
	    reset();
	}
	~Stats();
        /** Reset all statistics to 0. */
	void reset();
	Stats & operator+=(const Stats &from);
	Stats & operator-=(const Stats &from);

	/// specify an extent type if this statistic was entirely for one
	/// type.
	void printText(std::ostream &to, const std::string &extent_type = "");

    private:
	uint32_t use_count; // how many updates are pending to this stats?
	// You should grab the lock before calling this
	void update(uint32_t unpacked_size, uint32_t unpacked_fixed,
		    uint32_t unpacked_variable_raw,
		    uint32_t unpacked_variable, uint32_t packed_size,
		    uint32_t packed_variable_size, size_t nrecords, double pack_time, 
		    unsigned char fixed_compress_mode,
		    unsigned char variable_compress_mode);
	void updateCompressMode(unsigned char compress_mode);
	friend class DataSeriesSink;
    };

    /** \arg filename is the name of the file to write to.

        \arg compression_modes Indicates which compression
            algorithms should be tried.  See \link Extent_compress Extent::compress \endlink

        \arg compression_level Should be between 1 to 9 inclusive. The default of
            9 gives the best compression in general.  See the documentation of the
            underlying compression libraries for detail. 
            
    */
    DataSeriesSink(const std::string &filename,
		   int compression_modes = Extent::compress_all,
		   int compression_level = 9);

    /** automatically calls close() if close has not already been called. */
    ~DataSeriesSink();

    /** Sets a callback function for when extents are written out. */
    void setExtentWriteCallback(boost::function<void (off64_t, Extent &)> callback)
    { extent_write_callback = callback; }

    /** Blocks until all queued extents have been written and closes the file.  An
        @c ExtentTypeLibrary must have been written using
        \link DataSeriesSink::writeExtentLibrary writeExtentLibrary \endlink. */
    void close(bool do_fsync = false);

    /** Writes the ExtentTypes that are used in the file.  This function
        must be called exactly once before calling writeExtent.  The
        @c ExtentTypeLibrary must contain all of the types of Extents
        that will be used.  It is ok if it contains ExtentTypes that
        are not used. */
    void writeExtentLibrary(const ExtentTypeLibrary &lib);

    /** Add an @c Extent to the write queue.

        writeExtent is thread safe, although obviously the precise
        order of writing the extents can't be guarenteed if two
        writeExtent calls are executing at the same time.

        \arg e writeExtent is destructive, the contents of e will be
            moved (not copied) to the queue, so e will be empty on return.
        
        \arg toUpdate If toUpdate is not NULL, it will be updated
            with statistics about writing e, when e is actually written.
            
        \warning A copy of the pointer toUpdate is saved, so the pointee must
            not be destroyed, without either closing the file or calling
            \link DataSeriesSink::removeStatsUpdate removeStatsUpdate \endlink
            first
            
        \pre \link DataSeriesSink::writeExtentLibrary writeExtentLibrary
            \endlink must have been called and the @c ExtentTypeLibrary passed
            to it must contain the type of e. */
    void writeExtent(Extent &e, Stats *toUpdate);

    /** Block until all Extents in the queue have been written.
        If another thread is writing extents at the same time, this could
        wait forever. */
    void flushPending();

    /** Returns combined stats for all the Extents that have been written
        so far.  (Meaning actually written, not just queued).
        You are only guaranteed to have all the statistics for writing
        this file after you call close(). */
    Stats getStats();

    /** If you are going to delete a Stats that you previously used to
        write to a DataSeriesFile before you close the file, you need
        to call this. */
    void removeStatsUpdate(Stats *would_update);

    static void verifyTail(ExtentType::byte *data, bool need_bitflip,
			   const std::string &filename);
    
    /** Sets the number of threads that each @c DataSeriesSink uses to
        compress Extents.
        compressor_count == -1 ==> use # cpus. 0 ==> no threading;
        Only affects \link DataSeriesSink DataSeriesSinks \endlink
        created after a call. */
    static void setCompressorCount(int compressor_count = -1);

    const std::string &getFilename() const {
	return filename;
    }

    void setMaxBytesInProgress(size_t nbytes);
private:
    struct toCompress {
	Extent extent;
	Stats *to_update;
	bool in_progress;
	uint32_t checksum;
	Extent::ByteArray compressed;
	toCompress(Extent &e, Stats *_to_update)
	    : extent(e.type), to_update(_to_update), 
	      in_progress(false), checksum(0) 
	{
	    extent.swap(e);
	}
	void wipeExtent() {
	    Extent tmp(extent.type);
	    extent.swap(tmp);
	}
	bool readyToWrite() {
	    return compressed.size() > 0 && !in_progress;
	}
    };
    Stats stats;

    off64_t cur_offset; // set to -1 when sink is closed
    ExtentSeries index_series;
    Extent index_extent;
    Int64Field field_extentOffset;
    Variable32Field field_extentType;
    bool wrote_library;
    // returns the size of the compressed extent, needed for writing the
    // tail of the dataseries file.
    void checkedWrite(const void *buf, int bufsize);
    void writeExtentType(ExtentType &et);
    std::map<const ExtentType *,bool> valid_types;
    int fd;
    const int compression_modes;
    const int compression_level;
    uint32_t chained_checksum; 

    bool canQueueWork() {
	return bytes_in_progress < max_bytes_in_progress &&
	    pending_work.size() < 2 * compressors.size();
    }
    void queueWriteExtent(Extent &e, Stats *to_update);
    void lockedProcessToCompress(toCompress *work);
    void writeOutPending(bool have_lock = false); // always exits without lock held.
    static int compressor_count;
    std::vector<PThread *> compressors;
    PThread *writer;
    size_t bytes_in_progress, max_bytes_in_progress;
    bool shutdown_workers;
    Deque<toCompress *> pending_work;
    PThreadMutex mutex; // this mutex is ordered after Stats::getMutex(), so grab it second if you need both.
    PThreadCond available_queue_cond, available_work_cond, 
				   available_write_cond;
    boost::function<void (off64_t, Extent &)> extent_write_callback;

    const std::string filename;
    friend class DataSeriesSinkPThreadCompressor;
    void compressorThread();
    friend class DataSeriesSinkPThreadWriter;
    void writerThread();
};

inline DataSeriesSink::Stats 
operator-(const DataSeriesSink::Stats &a, const DataSeriesSink::Stats &b) {
    DataSeriesSink::Stats ret = a;
    ret -= b;
    return ret;
}

#endif
