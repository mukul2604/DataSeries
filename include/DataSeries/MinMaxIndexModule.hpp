// -*-C++-*-
/*
   (c) Copyright 2004-2005, Hewlett-Packard Development Company, LP

   See the file named COPYING for license details
*/

/** @file
    A module which uses the min/max index generated by dsextentindex to 
    pick out the appropriate extents
*/

#ifndef __DATASERIES_MINMAXINDEXMODULE_H
#define __DATASERIES_MINMAXINDEXMODULE_H

#include <DataSeries/IndexSourceModule.hpp>
#include <DataSeries/GeneralField.hpp>

/** \brief Selects Extents that have fields in particular range.

 * dsextentindex generates an index file which has a min/max extent in
 * it that tells the minimum and the maximum for a bunch of fields for
 * each of a collection of extents in a bunch of files.  This module
 * will do a range overlap between two values and the min/max for two
 * different fields, and will then sort by either the min or the max
 * value associated with each of the extents.
 */

class MinMaxIndexModule : public IndexSourceModule {
public:
    /** selects extents where [minv..maxv] overlaps with
    [min_fieldname..max_fieldname] and returns the extents sorted by
    sort_fieldname with ties broken by filename and extent offset the
    module will automatically put the min: and max: on the min/max
    fieldnames, you need to put it on the sort one as neither choice
    makes generic sense */
    MinMaxIndexModule(const std::string &index_filename,
		      const std::string &index_type, 
		      const GeneralValue minv, 
		      const GeneralValue maxv,
		      const std::string &min_fieldname,
		      const std::string &max_fieldname,
		      const std::string &sort_fieldname);

    /** structure for defining an overlap range, caller defines
	minv, maxv, min_fieldname, max_fieldname */
    struct selector {
	// 2004-10-28, anderse: wanted to make the next 4 variables
	// const, but doing so means that the tmp.push_back(foo) in
	// the simple constructor throws some weird C++ error that I
	// can't understand enough to know how to fix it.
	GeneralValue minv, maxv;
	std::string min_fieldname, max_fieldname;

	GeneralField *minf, *maxf;
	selector(const GeneralValue &a, const GeneralValue &b,
		 const std::string &c, const std::string &d) 
	    : minv(a), maxv(b), 
	    min_fieldname(c), max_fieldname(d), 
	    minf(NULL), maxf(NULL) { }
    };

    struct kept_extent {
	std::string filename;
	ExtentType::int64 extent_offset;
	GeneralValue sortvalue;
	kept_extent(const std::string &a,ExtentType::int64 b,GeneralValue &c)
	    : filename(a), extent_offset(b), sortvalue(c) { }
    };

    class kept_extent_bysortvalue {
    public:
	bool operator() (const kept_extent &a, const kept_extent &b) const {
	    return a.sortvalue < b.sortvalue;
	}
    };


    /** selects extents where selectors overlap.  If use_or is false,
        then it selects extents where all selectors overlap
        (intersection).  If use_or is true, if selects extents where
        any of the selectors overlap (union).  Rules for the values as
        per the other constructor */
    MinMaxIndexModule(const std::string &index_filename,
                      const std::string &index_type,
                      std::vector<selector> intersection_list,
                      const std::string &sort_fieldname,
                      const bool _use_or);

    /** selects extents where all selectors overlap (intersection).
        Rules for the values as per the other constructor */
    MinMaxIndexModule(const std::string &index_filename,
                      const std::string &index_type,
                      std::vector<selector> intersection_list,
                      const std::string &sort_fieldname);

protected:
    virtual void lockedResetModule();

    virtual PrefetchExtent *lockedGetCompressedExtent();

private:
    void init(const std::string &index_filename,
	      std::vector<selector> &intersection_list,
	      const std::string &sort_fieldname);

    std::vector<kept_extent> kept_extents;
    const std::string index_type;
    unsigned cur_extent;
    DataSeriesSource *cur_source;
    std::string cur_source_filename;
    bool use_or;
};

#endif
