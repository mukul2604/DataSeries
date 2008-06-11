// -*-C++-*-
/*
   (c) Copyright 2003-2006, Hewlett-Packard Development Company, LP

   See the file named COPYING for license details
*/

/** @file
    Select subset of fields from a collection of traces, generate a new trace
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Lintel/AssertBoost.hpp>
#include <Lintel/StringUtil.hpp>

#include <DataSeries/commonargs.hpp>
#include <DataSeries/DataSeriesFile.hpp>
#include <DataSeries/GeneralField.hpp>
#include <DataSeries/DataSeriesModule.hpp>
#include <DataSeries/TypeIndexModule.hpp>

using namespace std;
using boost::format;

int
main(int argc, char *argv[])
{
    Extent::setReadChecksFromEnv(true); // going to be compressing, may as well check
    commonPackingArgs packing_args;
    getPackingArgs(&argc,argv,&packing_args);

    INVARIANT(argc > 4,
	     format("Usage: %s <common-args> type-prefix field,field,field,..."
		    "input-filename... output-filename") % argv[0]);
    string type_prefix(argv[1]);
    string fieldlist(argv[2]);
    
    vector<string> fields;

    split(fieldlist,",",fields);

    {
	struct stat buf;
	INVARIANT(stat(argv[argc-1],&buf) != 0, 
		  format("Refusing to overwrite existing file %s.")
		  % argv[argc-1]);
    }
    DataSeriesSink output(argv[argc-1],packing_args.compress_modes,packing_args.compress_level);

    // to get the complete typename and type information...
    DataSeriesSource first_file(argv[3]);
    const ExtentType *intype = first_file.mylibrary.getTypeByPrefix(type_prefix);
    first_file.closefile();
    INVARIANT(intype != NULL,
	      boost::format("can not find a type matching prefix %s")
	      % type_prefix);

    TypeIndexModule source(intype->getName());
    for(int i=3;i<(argc-1);++i) {
	source.addSource(argv[i]);
    }
    source.startPrefetching();

    ExtentSeries inputseries(ExtentSeries::typeLoose);
    ExtentSeries outputseries(ExtentSeries::typeLoose);
    vector<GeneralField *> infields, outfields;

    // TODO: figure out how to handle pack_relative options that are
    // specified relative to a field that was not selected.  Right
    // now, there is no way to do this selection; try replacing
    // enter_driver with leave_driver in
    // src/Makefile.am:ran.check-dsselect. Also, give an option for the
    // xml description to be specified on the command line.

    string xmloutdesc("<ExtentType name=\"");
    xmloutdesc.append(type_prefix);
    xmloutdesc.append("\">\n");
    INVARIANT(intype != NULL, "bad");
    inputseries.setType(*intype);
    for(vector<string>::iterator i = fields.begin();
	i != fields.end();++i) {
	xmloutdesc.append("  ");
	printf("%s -> %s\n",i->c_str(),intype->xmlFieldDesc(*i).c_str());
	xmloutdesc.append(intype->xmlFieldDesc(*i));
	xmloutdesc.append("\n");
	infields.push_back(GeneralField::create(NULL,inputseries,*i));
    }
    xmloutdesc.append("</ExtentType>\n");
    printf("%s\n",xmloutdesc.c_str());

    ExtentTypeLibrary library;
    const ExtentType *outputtype = library.registerType(xmloutdesc);
    output.writeExtentLibrary(library);
    INVARIANT(outputtype != NULL, "bad");
    outputseries.setType(*outputtype);
    for(vector<string>::iterator i = fields.begin();
	i != fields.end();++i) {
	outfields.push_back(GeneralField::create(NULL,outputseries,*i));
    }

    OutputModule outmodule(output,outputseries,outputtype,
			   packing_args.extent_size);
    while(true) {
	Extent *inextent = source.getExtent();
	if (inextent == NULL)
	    break;
	for(inputseries.setExtent(inextent);inputseries.pos.morerecords();
	    ++inputseries.pos) {
	    outmodule.newRecord();
	    for(unsigned int i=0;i<infields.size();++i) {
		outfields[i]->set(infields[i]);
	    }
	}
	delete inextent;
    }
    outmodule.flushExtent();
    outmodule.close();
    
    GeneralField::deleteFields(infields);
    GeneralField::deleteFields(outfields);
    return 0;
}

    