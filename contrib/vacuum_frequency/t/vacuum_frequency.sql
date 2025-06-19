-- Test vacuum_frequency extension (C-level hooks only)

-- Create the extension
CREATE EXTENSION vacuum_frequency;

-- Create test tables to verify the extension loads correctly
CREATE TABLE test_table1 (id serial PRIMARY KEY, data text);
CREATE TABLE test_table2 (id serial PRIMARY KEY, data text);

-- Insert some data to generate dead tuples
INSERT INTO test_table1 (data) VALUES ('test data 1'), ('test data 2');
INSERT INTO test_table2 (data) VALUES ('test data 3'), ('test data 4');

-- Update to create dead tuples
UPDATE test_table1 SET data = 'updated data 1' WHERE id = 1;
UPDATE test_table2 SET data = 'updated data 3' WHERE id = 1;

-- Verify extension is loaded
SELECT extname FROM pg_extension WHERE extname = 'vacuum_frequency';

-- Clean up
DROP TABLE test_table1, test_table2; 