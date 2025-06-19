-- Create the vacuum_frequency extension
-- This extension provides C-level hooks for controlling VACUUM frequency
-- No SQL functions are exposed - control is handled entirely through C hooks

CREATE EXTENSION vacuum_frequency; 