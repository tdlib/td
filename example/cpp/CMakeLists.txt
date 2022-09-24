cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

project(TdExample VERSION 1.0 LANGUAGES CXX)

find_package(Td 1.8.6 REQUIRED)

add_executable(tdjson_example tdjson_example.cpp)
target_link_libraries(tdjson_example PRIVATE Td::TdJson)
set_property(TARGET tdjson_example PROPERTY CXX_STANDARD 11)

add_executable(td_example td_example.cpp)
target_link_libraries(td_example PRIVATE Td::TdStatic)
set_property(TARGET td_example PROPERTY CXX_STANDARD 14)
SQL> spool annotateInstance.log
SQL> --
SQL> @setUser
SQL> --
SQL> define USERNAME = KORFSA
SQL> --
SQL> def PASSWORD = KORFSA
SQL> --
SQL> --
SQL> connect &USERNAME/&PASSWORD
Connected.
SQL> --
SQL> declare
  2    xmlSchema xmltype;
  3    res       boolean;
  4    schemaDoc varchar2(256);
  5
  6  begin
  7    schemaDoc := '/home/&USERNAME/xsd/xbrl-instance-2003-12-31.xsd';
  8    xmlschema := xdburitype(schemaDoc).getXML();
  9
 10    xdb_annotate_schema.addXDBNamespace(xmlschema);
 11    xdb_annotate_schema.addDefaultTable(xmlschema, 'xbrl','XBRL_TABLE');
 12    xdb_annotate_schema.disableDefaultTables(xmlschema);
 13
 14    select insertChildXML
 15         (
 16           xmlSchema,
 17           '/schema/element[@name="xbrl"]',
 18           '@xdb:SQLType',
