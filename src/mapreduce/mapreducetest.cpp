/*
 * mapreducetest.cpp
 *
 *  Created on: Apr 30, 2009
 *      Author: shirant
 */

#include <string>

#include <Lintel/LintelLog.hpp>

#include <DataSeries/TypeIndexModule.hpp>
#include <DataSeries/DataSeriesFile.hpp>
#include <DataSeries/MemorySortModule.hpp>
#include <DataSeries/SortModule.hpp>
#include <DataSeries/Extent.hpp>

class StringFieldComparator {
public:
    bool operator()(const Variable32Field &field0, const Variable32Field &field1) {
        return field0.stringval() < field1.stringval(); // not efficient, but does it matter?
    }
};

int main(int argc, const char *argv[]) {
    LintelLog::parseEnv();
    LintelLogDebug("mapreducedemo", "Hi!");

    TypeIndexModule inputModule("Text");
    inputModule.addSource(argv[1]);

    //MemorySortModule<Variable32Field> memorySortModule(inputModule, "line", StringFieldComparator(), 1 << 20);
    //SortModule memorySortModule(inputModule, "line", StringFieldComparator(), 1 << 20, 1 << 30, "/tmp/sort");
    SortModule memorySortModule(inputModule, "line", StringFieldComparator(), 100 * 1000, 1 << 20, "/tmp/sort");

    DataSeriesSink sink(argv[2], Extent::compress_none, 0);

    bool wroteLibrary = false;
    Extent *extent = NULL;
    while ((extent = memorySortModule.getExtent()) != NULL) {
        if (!wroteLibrary) {
            ExtentTypeLibrary library;
            library.registerType(extent->getType());
            sink.writeExtentLibrary(library);
            wroteLibrary = true;
        }
        sink.writeExtent(*extent, NULL);
        delete extent;
    }
    sink.close();
}
