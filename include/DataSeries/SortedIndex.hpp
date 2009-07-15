/*
   (c) Copyright 2009, Hewlett-Packard Development Company, LP

   See the file named COPYING for license details
*/

/** @file
    A module which uses the min/max index generated by dsextentindex
    to pick out the appropriate extents. Unlike MinMaxIndex, maintains
    index in memory, and only builds index on a single value. TODO:
    make it work for multiple values.
*/

#ifndef DATASERIES_SORTED_INDEX_HPP
#define DATASERIES_SORTED_INDEX_HPP

#include <boost/shared_ptr.hpp>

#include <DataSeries/GeneralField.hpp>

/** SortedIndex assumes that for each file referenced by the
    index, that files rows are sorted on the index field. It is an error to
    violate this rule. */
class SortedIndex {
public:
    /** Create a new SortedIndex
	@param index_filename The file containing the index (created by 
	dsextent index)
	@param index_type The type of the extent indexed
	@param fieldname The name of the field to use as index
     */
    SortedIndex(const std::string &index_filename,
		      const std::string &index_type,
		      const std::string &fieldname);
    /** Destructor */
    virtual ~SortedIndex();

    // IndexEntry describes a single extent in the index
    struct IndexEntry {
	uint64_t source; 	// Source file ID to be matched with ID vector
	GeneralValue minv;	// min value of index field in extent
	GeneralValue maxv;	// max value of index field in extent
	uint64_t offset;	// offset of extent in source
	IndexEntry(uint64_t source,
		   const GeneralValue &minv, const GeneralValue &maxv, 
		   uint64_t offset) 
	    : source(source), minv(minv), maxv(maxv), offset(offset) 
	{}
	// Consider the following entry values for minv, maxv:
	// 
	// [ 1, 4 ]
	// [ 5, 7 ]
	// [ 7, 11 ]
	//
	// When we search for 7, we want to find the first extent
	// containing 7, which means that we have to compare based on
	// maxv rather than minv.
	bool operator<(const GeneralValue &rhs) const {
	    return maxv < rhs;
	}

	bool inRange(const GeneralValue &v) const {
	    return v >= minv && v <= maxv;
	}
    };

    
    /** Searches the index. Returns a pointer to the vector of extents
	that match the search to be handed off to an
	ExtentVectorModule for fetching from disk.  TODO: Change to
	take a vector of GeneralValues to match against.
    */
    std::vector<IndexEntry*>* search(const GeneralValue &value);

    /** Returns the file_names vector.  Used as a cache of
	id->filename translations to avoid copying strings around.
     */
    std::vector<std::string>& getFileNames()
    { return file_names; }

    const std::string& getIndexType()
    { return index_type; }

protected:
private:
    typedef std::vector<IndexEntry> IndexEntryVector;
    const std::string index_type;	// type of extent indexed
    std::vector<IndexEntryVector> index;// one index for each indexed file
    std::vector<std::string> file_names; // vector of source filenames. Index is ID
};

#endif
