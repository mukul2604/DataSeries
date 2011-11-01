#!/usr/bin/perl -w
use strict;
use sort 'stable';
use Carp;
use Data::Dumper;
use Text::Table;

use lib "$ENV{BUILD_OPT}/DataSeries.server/src/server/gen-perl";
use lib "$ENV{HOME}/projects/thrift/lib/perl/lib";

use Thrift::BinaryProtocol;
use Thrift::Socket;
use Thrift::BufferedTransport;

use DataSeriesServer;

my $debug = 0;

$|=1;
my $socket = new Thrift::Socket('localhost', 49476);
$socket->setRecvTimeout(1000*1000);
my $transport = new Thrift::BufferedTransport($socket, 4096, 4096);
my $protocol = new Thrift::BinaryProtocol($transport);
my $client = new DataSeriesServerClient($protocol);

$transport->open();
$client->ping();
print "Post Ping\n" if $debug;

# tryImportCSV();
# tryImportSql();
# tryImportData();
# tryHashJoin();
# trySelect();
# tryProject();
tryUpdate();
testSimpleStarJoin();
# tryStarJoin();
testUnion();
testSort();

sub importData ($$$) {
    my ($table_name, $table_desc, $rows, $quiet) = @_;

    confess "?" unless defined $rows;
    my $data_xml;
    if (ref $table_desc) {
        $data_xml = qq{<ExtentType name="$table_name" namespace="simpl.hpl.hp.com" version="1.0">\n};
        for (my $i=0; $i < @$table_desc; $i += 2) {
            my $name = $table_desc->[$i];
            my $desc = $table_desc->[$i+1];
            my $extra = $desc eq 'variable32' ? 'pack_unique="yes" ' : '';
            $data_xml .= qq{  <field type="$desc" name="$name" $extra/>\n};
        }
        $data_xml .= "</ExtentType>\n";
        print "Importing with $data_xml\n" if $debug;
    } else {
        $data_xml = $table_desc;
    }
    $client->importData($table_name, $data_xml, new TableData({ 'rows' => $rows }));
}

sub getTableData ($;$$) {
    my ($table_name, $max_rows, $where_expr) = @_;

    $max_rows ||= 1000;
    $where_expr ||= '';
    my $ret = eval { $client->getTableData($table_name, $max_rows, $where_expr); };
    if ($@) {
        print Dumper($@);
        die "fail";
    }
    return $ret;
}

sub printTable ($) {
    my ($table) = @_;
    my $table_refr = getTableData($table);

    my @columns;
    foreach my $col (@{$table_refr->{'columns'}}) {
        push(@columns, $col->{'name'} . "\n" . $col->{'type'});
    }

    my $tb = Text::Table->new(@columns);
    foreach my $row (@{$table_refr->{'rows'}}) {
        $tb->add(@{$row});
    }
    print "Table: $table \n\n";
    print $tb, "\n\n";
}

sub checkTable ($$$) {
    my ($table_name, $columns, $data) = @_;
    printTable($table_name) if $debug;
    my $table = getTableData($table_name, 10000000);

    die scalar @$columns/2 . " != " . scalar @{$table->{columns}}
        unless @$columns/2 == @{$table->{columns}};
    for (my $i = 0; $i < @$columns/2; ++$i) {
        my ($name, $type) = ($columns->[2*$i], $columns->[2*$i+1]);
        my $col = $table->{columns}->[$i];
        die "name mismatch col $i: $name != $col->{name}" unless $name eq $col->{name};
        die "type mismatch col $i: $type != $col->{type}" unless $type eq $col->{type};
    }

    my $rows = $table->{rows};
    die scalar @$rows . " != " . scalar @$data unless @$rows == @$data;
    for (my $i = 0; $i < @$data; ++$i) {
        my $ra = $data->[$i];
        my $rb = $rows->[$i];
        die "?" unless @$ra == @$rb;
        for (my $j = 0; $j < @$ra; ++$j) {
            die "$i: " . join(",", @$ra) . " != " . join(",", @$rb) unless $ra->[$j] eq $rb->[$j];
        }
    }
}


sub tryImportCSV {
    my $csv_xml = <<'END';
<ExtentType name="test-csv2ds" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="bool" name="bool" />
  <field type="byte" name="byte" />
  <field type="int32" name="int32" />
  <field type="int64" name="int64" />
  <field type="double" name="double" />
  <field type="variable32" name="variable32" pack_unique="yes" />
</ExtentType>
END

    $client->importCSVFiles(["/home/anderse/projects/DataSeries/check-data/csv2ds-1.csv"],
                            $csv_xml, 'csv2ds-1', ",", "#");
    print "imported\n";

    my $ret = getTableData('csv2ds-1');
    print Dumper($ret);
}

sub tryImportSql {
    $client->importSQLTable('', 'scrum_task', 'scrum_task');

    my $ret = getTableData('scrum_task');
    print Dumper($ret);
}

sub tryImportData {
    my $data_xml = <<'END';
<ExtentType name="test-import" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="bool" name="bool" />
  <field type="byte" name="byte" />
  <field type="int32" name="int32" />
  <field type="int64" name="int64" />
  <field type="double" name="double" />
  <field type="variable32" name="variable32" pack_unique="yes" />
</ExtentType>
END

    $client->importData("test-import", $data_xml, new TableData
                        ({ 'rows' =>
                           [[ 'on', 5, 1371, 111111, 11.0, "abcde" ],
                            [ 'on', 5, 1371, 111111, 11.0, "1q2w3e" ],
# TODO: the next row tests the hash-map with a missing match, but fails if used in the star join.
# each of the tests should just manufacture the tables they want (easy now with importData()) so that
# changes to one test won't randomly break other tests.
#                            [ 'on', 6, 1385, 112034, 12.0, "mnop" ],
                            [ 'off', 7, 12345, 999999999999, 123.456, "fghij" ] ]}));
}

sub tryHashJoin {
    tryImportData();

    importData('join-data', [ 'join_int32' => 'int32', 'join_str' => 'variable32' ],
               [[ 1371, "123" ],
                [ 1371, "456" ],
                [ 1371, "789" ],
                [ 9321, "xyz" ],
                [ 12345, "fghij" ] ]);

    print "\n----- Table A ----\n";
    print Dumper(getTableData("test-import"));
    print "\n---- Table B ----\n";
    print Dumper(getTableData("join-data"));


    $client->hashJoin('test-import', 'join-data', 'test-hash-join',
                      { 'int32' => 'join_int32' }, { 'a.int32' => 'table-a:join-int32',
                                                     'a.variable32' => 'table-a:extra-variable32',
                                                     'b.join_int32' => 'table-b:join-int32',
                                                     'b.join_str' => 'table-b:extra-variable32'});
    print "\n---- HashJoin Output ----\n";
    print Dumper(getTableData("test-hash-join"));
}

sub trySelect {
    tryImportData();
    $client->selectRows("test-import", "test-select", "int32 == 1371");
    print Dumper(getTableData('test-select'));

    print Dumper(getTableData('test-import', undef, 'int32 == 1371'));
}

sub tryProject {
    tryImportData();

    $client->projectTable('test-import', 'test-project', [ qw/bool int32 variable32/ ]);
    print Dumper(getTableData('test-project'));
}

sub tryUpdate {
    print "testing update...";
    my $base_fields = <<'END';
  <field type="variable32" name="type" pack_unique="yes" />
  <field type="int32" name="count" />
END

    my $base_xml = <<"END";
<ExtentType name="base-table" namespace="simpl.hpl.hp.com" version="1.0">
$base_fields
</ExtentType>
END

    my $update_xml = <<"END";
<ExtentType name="base-table" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="byte" name="update-op" />
$base_fields
</ExtentType>
END

    $client->importData("base-table", $base_xml, new TableData
                        ({ 'rows' =>
                           [[ "abc", 5 ],
                            [ "def", 6 ],
                            [ "ghi", 0 ],
                            [ "jkl", 7 ], ]}));

    $client->importData("update-table", $update_xml, new TableData
                        ({ 'rows' =>
                           [[ 1, "aaa", 3 ],
                            [ 2, "abc", 9 ],
                            [ 3, "def", 0 ],
                            [ 2, "ghi", 5 ] ]}));

    $client->sortedUpdateTable('base-table', 'update-table', 'update-op', [ 'type' ]);
    checkTable('base-table', [qw/type variable32  count int32/],
               [ [ qw/aaa 3/ ], [ qw/abc 9/ ], [ qw/ghi 5/ ], [ qw/jkl 7/ ] ]);

    $client->importData("update-table", $update_xml, new TableData
                        ({ 'rows' =>
                           [[ 3, "aaa", 0 ],
                            [ 2, "mno", 1 ],
                            [ 1, "pqr", 2 ] ]}));
    $client->sortedUpdateTable('base-table', 'update-table', 'update-op', [ 'type' ]);
    checkTable('base-table', [qw/type variable32  count int32/],
               [ [ qw/abc 9/ ], [ qw/ghi 5/ ], [ qw/jkl 7/ ], [ qw/mno 1 /], [qw/pqr 2/] ]);

    print "passed.\n";
    # TODO: Add test for empty update..., and in general for empty extents in all the ops
}

sub tryStarJoin {
    tryImportData(); # columns bool, byte, int32, int64, double, variable32; 3 rows

    my $data_xml = <<'END';
<ExtentType name="dim-int" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="int32" name="key_1" />
  <field type="int32" name="key_2" />
  <field type="variable32" name="val_1" pack_unique="yes" />
  <field type="variable32" name="val_2" pack_unique="yes" />
</ExtentType>
END

    printTable("test-import");
    $client->importData("dim-int", $data_xml, new TableData
                        ({ 'rows' =>
                           [[ 1371, 12345, "abc", "def" ],
                            [ 12345, 1371, "ghi", "jkl" ],
                            [ 999, 999, "mno", "pqr" ]] }));
    printTable("dim-int");

    # Same table dim-int, is used to create 2 different dimensions. In practice we could have
    # different table and selectively use columns from each table.
    my $dim_1 = new Dimension({ dimension_name => 'dim_int_1',
                                source_table => 'dim-int',
                                key_columns => ['key_1'],
                                value_columns => ['val_1', 'val_2'],
                                max_rows => 1000 });
    my $dim_2 = new Dimension({ dimension_name => 'dim_int_2',
                                source_table => 'dim-int',
                                key_columns => ['key_2'],
                                value_columns => ['val_1', 'val_2'],
                                max_rows => 1000 });

    my $dfj_1 = new DimensionFactJoin({ dimension_name => 'dim_int_1',
                                        fact_key_columns => [ 'int32' ],
                                        extract_values => { 'val_1' => 'dfj_1.dim1_val1' },
                                        missing_dimension_action => DFJ_MissingAction::DFJ_Unchanged });

    my $dfj_2 = new DimensionFactJoin({ dimension_name => 'dim_int_1',
                                        fact_key_columns => [ 'int32' ],
                                        extract_values => { 'val_1' => 'dfj_2.dim1_val1',
                                                            'val_2' => 'dfj_2.dim1_val2' },
                                        missing_dimension_action => DFJ_MissingAction::DFJ_Unchanged });

    my $dfj_3 = new DimensionFactJoin({ dimension_name => 'dim_int_2',
                                        fact_key_columns => [ 'int32' ],
                                        extract_values => { 'val_2' => 'dfj_3.dim1_val2' },
                                        missing_dimension_action => DFJ_MissingAction::DFJ_Unchanged });

    my $dfj_4 = new DimensionFactJoin({ dimension_name => 'dim_int_2',
                                        fact_key_columns => [ 'int32' ],
                                        extract_values => { 'val_1' => 'dfj_4.dim1_val1' },
                                        missing_dimension_action => DFJ_MissingAction::DFJ_Unchanged });

    $client->starJoin('test-import', [$dim_1, $dim_2], 'test-star-join',
                      { 'int32' => 'f.int_32', 'int64' => 'f.int_64'}, [$dfj_1, $dfj_2, $dfj_3, $dfj_4]);

    printTable("test-star-join");
    # print Dumper(getTableData("test-star-join"));
}



sub testSimpleStarJoin {
    print "testing simple star join...";
    my $person_xml = <<'END';
<ExtentType name="person-details" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="variable32" name="Name" pack_unique="yes" />
  <field type="variable32" name="Country" pack_unique="yes" />
  <field type="variable32" name="State" pack_unique="yes" />
</ExtentType>
END

    my $country_xml = <<'END';
<ExtentType name="country-details" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="variable32" name="Name" pack_unique="yes" />
  <field type="variable32" name="Capital" pack_unique="yes" />
  <field type="variable32" name="Currency" pack_unique="yes" />
</ExtentType>
END

    my $state_xml = <<'END';
<ExtentType name="country-details" namespace="simpl.hpl.hp.com" version="1.0">
  <field type="variable32" name="Name" pack_unique="yes" />
  <field type="variable32" name="Capital" pack_unique="yes" />
  <field type="variable32" name="TimeZone" pack_unique="yes" />
</ExtentType>
END

    $client->importData("person-details", $person_xml, new TableData
                        ({ 'rows' =>
                           [[ "John", "United States", "California" ],
                            [ "Adam", "United States", "Colarado" ],
                            [ "Ram", "India", "Karnataka"],
                            [ "Shiva", "India", "Maharastra"]] }));

    $client->importData("country-details", $country_xml, new TableData
                        ({ 'rows' =>
                           [[ "India", "New Delhi", "INR" ],
                            [ "United States", "Washington, D.C.", "Dollar"]] }));

    $client->importData("state-details", $state_xml, new TableData
                        ({ 'rows' =>
                           [[ "California", "Sacramento", "PST (UTC.8), PDT (UTC.7)" ],
                            [ "Colarado", "Denver", "MST=UTC-07, MDT=UTC-06" ],
                            [ "Karnataka", "Bangalore", "IST" ],
                            [ "Maharastra", "Mumbai", "IST" ]] }));

    if ($debug) {
        printTable("person-details");
        printTable("country-details");
        printTable("state-details");
    }

    # Same table dim-int, is used to create 2 different dimensions. In practice we could have
    # different table and selectively use columns from each table.
    my $dim_country = new Dimension({ dimension_name => 'dim_country',
                                      source_table => 'country-details',
                                      key_columns => ['Name'],
                                      value_columns => ['Capital'],
                                      max_rows => 1000 });

    my $dim_state = new Dimension({ dimension_name => 'dim_state',
                                    source_table => 'state-details',
                                    key_columns => ['Name'],
                                    value_columns => ['Capital'],
                                    max_rows => 1000 });

    my $dfj_1 = new DimensionFactJoin({ dimension_name => 'dim_country',
                                        fact_key_columns => ['Country'],
                                        extract_values => { 'Capital' => 'CountryCapital' },
                                        missing_dimension_action => DFJ_MissingAction::DFJ_Unchanged });

    my $dfj_2 = new DimensionFactJoin({ dimension_name => 'dim_state',
                                        fact_key_columns => [ 'State' ],
                                        extract_values => { 'Capital' => 'StateCapital' },
                                        missing_dimension_action => DFJ_MissingAction::DFJ_Unchanged });

    $client->starJoin('person-details', [$dim_country, $dim_state], 'test-star-join',
                      { 'Name' => 'Name', 'Country' => 'Country', 'State' => 'State'}, [$dfj_1, $dfj_2]);

    printTable("test-star-join") if $debug;
    checkTable('test-star-join', [ qw/Country variable32
                                      Name variable32
                                      State variable32
                                      CountryCapital variable32
                                      StateCapital variable32/ ],
               [ [ 'United States', 'John', 'California', 'Washington, D.C.', 'Sacramento' ],
                 [ 'United States', 'Adam', 'Colarado', 'Washington, D.C.', 'Denver' ],
                 [ 'India', 'Ram', 'Karnataka', 'New Delhi', 'Bangalore' ],
                 [ 'India', 'Shiva', 'Maharastra', 'New Delhi', 'Mumbai' ] ]);
    print "passed.\n";
}

sub unionTable {
    return new UnionTable({ 'table_name' => $_[0], 'extract_values' => $_[1] });
}

sub testUnion {
    print "testing union...";
    # extra column tests discard; different names tests rename
    my @data1 = ( [ 100, "abc", 3, 1 ],
                  [ 2000, "ghi", 4, 4 ],
                  [ 3000, "def", 5, 5 ],
                  [ 12345, "ghi", 17, 7 ] );

    importData('union-input-1', [ 'col1' => 'int32', 'col2' => 'variable32', 'col3' => 'byte',
                                  'col4' => 'int32' ], \@data1);
    my @data2 = ( [ 100, "def", 2 ],
                  [ 2000, "def", 3 ],
                  [ 12345, "efg", 6 ],
                  [ 12345, "ghi", 8 ],
                  [ 12345, "jkl", 9 ],
                  [ 20000, "abc", 11 ] );
    importData('union-input-2', [ 'cola' => 'int32', 'colb' => 'variable32', 'colc' => 'int32' ],
               \@data2);

    my @data3 = ( [ 10, "zyw", 0 ],
                  [ 20000, "aaa", 10 ]);

    importData('union-input-3', [ 'colm' => 'int32', 'coln' => 'variable32', 'colo' => 'int32' ],
               \@data3);

    $client->unionTables([ unionTable('union-input-1', { 'col1' => 'int', 'col2' => 'string',
                                                         'col4' => 'order' }),
                           unionTable('union-input-2', { 'cola' => 'int', 'colb' => 'string',
                                                         'colc' => 'order' }),
                         unionTable('union-input-3', { 'colm' => 'int', 'coln' => 'string',
                                                       'colo' => 'order' })],
                         [ qw/int string/ ], 'union-output');

    my @data = map { [ $_->[0], $_->[3], $_->[1] ] } @data1;
    push(@data, map { [ $_->[0], $_->[2], $_->[1] ] } @data2, @data3);
    @data = sort { $a->[1] <=> $b->[1] } @data;
    #printTable("union-input-1");
    #printTable("union-input-2");
    #printTable("union-input-3");
    checkTable("union-output", [ qw/int int32   order int32   string variable32/ ], \@data);
    print "passed.\n";
}

sub testSort {
    my $sc_0 = new SortColumn({ 'column' => 'col0', 'sort_mode' => SortMode::SM_Decending });
    my $sc_1 = new SortColumn({ 'column' => 'col1', 'sort_mode' => SortMode::SM_Ascending });
    my $sc_2 = new SortColumn({ 'column' => 'col2', 'sort_mode' => SortMode::SM_Decending });

    print "sort test 1...";
    my @data = ( [ 5000, "ghi" ],
                 [ 5000, "abc" ],
                 [ 12345, "abc" ],
                 [ 12345, "ghi" ],
                 [ 3000, "defg" ],
                 [ 3000, "de" ],
                 [ 3000, "def" ] );

    importData('sort-1', [ 'col1' => 'int32', 'col2' => 'variable32' ], \@data);


    $client->sortTable('sort-1', 'sort-out-1', [ $sc_1, $sc_2 ]);
    @data = sort { $a->[0] != $b->[0] ? $a->[0] <=> $b->[0] : $b->[1] cmp $a->[1] } @data;
    checkTable('sort-out-1', [ 'col1' => 'int32', 'col2' => 'variable32' ], \@data);
    print "passed.\n";

    print "big sort test...gen...";
    ## Now with a big test; annoyingly slow on the perl client side, but there you go.
    ## Would be slightly better with 100k rows, but then it's really slow; you can verify
    ## it is doing multi-extent merging by running LINTEL_LOG_DEBUG=SortModule ./server
    my $nrows = 10000;
    my @sort2 = map { [ rand() > 0.5 ? "true" : "false", int(rand(100)),
                        int(rand(100)), int(rand(100000)) ] } (1..$nrows);

    print "import...";
    my @cols2 = ( 'col0' => 'bool', 'col1' => 'byte', 'col2' => 'int32', 'col3' => 'int64' );
    importData('sort-2', \@cols2, \@sort2);
    print "sort-server...";
    $client->sortTable('sort-2', 'sort-out-2',  [ $sc_1, $sc_0, $sc_2 ]);
    # printTable('sort-out-2');
    # We get lucky on booleans 0 < 1 and 'false' < 'true'
    print "sort-client...";
    my @sorted2 = sort { $a->[1] != $b->[1] ? $a->[1] <=> $b->[1]
                             : ($a->[0] ne $b->[0] ? $b->[0] cmp $a->[0] : $b->[2] <=> $a->[2]) }
        @sort2;
    die "?" unless @sorted2 == $nrows;
    print "check...";
    checkTable('sort-out-2', \@cols2, \@sorted2);
    print "passed.\n";
}

# print "pong\n";
# $client->importDataSeriesFiles(["/home/anderse/projects/DataSeries/check-data/nfs.set6.20k.ds"],
# 			       "NFS trace: attr-ops", "import");
# print "import\n";
# $client->mergeTables(["import", "import"], "2ximport");
# $client->test();
# print "test\n";

