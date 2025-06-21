# Custom Autovacuum Extension

A PostgreSQL extension that provides a custom autovacuum policy framework, allowing you to override the default autovacuum decision logic with a single adaptive policy.

## Overview

This extension implements a proportional controller-based autovacuum policy that dynamically adjusts vacuum aggressiveness based on the rate of dead tuple accumulation. The policy:

- **Adapts to workload patterns**: More aggressive vacuuming when dead tuples accumulate quickly
- **Minimizes overhead**: Conservative vacuuming when dead tuples accumulate slowly  
- **Uses proportional control**: Dynamically adjusts thresholds based on accumulation rate
- **Provides cost control**: Adjusts vacuum cost limits and delays based on workload

## Installation

### 1. Build the Extension

```bash
cd contrib/custom_autovacuum
make
make install
```

### 2. Configure PostgreSQL

Add to `postgresql.conf`:

```ini
shared_preload_libraries = 'custom_autovacuum'
```

### 3. Create the Extension

```sql
CREATE EXTENSION custom_autovacuum;
```

## How It Works

The extension hooks into PostgreSQL's autovacuum decision process and replaces the default threshold-based logic with an adaptive policy:

### Proportional Controller Logic

1. **Calculate accumulation rate**: Dead tuples per second since last vacuum
2. **Compare to target rate**: Default target is 0.1 dead tuples/second
3. **Adjust threshold**: Lower threshold for high accumulation, higher for low accumulation
4. **Set cost parameters**: Aggressive settings for high rates, conservative for low rates

### Policy Behavior

- **High accumulation rate** (>0.2 dead tuples/sec): Aggressive vacuum (cost_limit=2000, delay=0)
- **Moderate accumulation rate** (0.1-0.2 dead tuples/sec): Balanced vacuum (cost_limit=1000, delay=5ms)
- **Low accumulation rate** (<0.1 dead tuples/sec): Conservative vacuum (cost_limit=500, delay=10ms)

## Configuration

### GUC Variables

- `custom_autovacuum.enabled` (boolean): Enable/disable the custom policy (default: true)

### Example Configuration

```sql
-- Enable custom autovacuum policy
SET custom_autovacuum.enabled = true;

-- Check current setting
SHOW custom_autovacuum.enabled;
```

## Monitoring

Custom policy decisions are logged at DEBUG2 level. Enable debug logging to see policy decisions:

```sql
SET log_min_messages = 'debug2';
```

For detailed proportional controller decisions, use DEBUG3:

```sql
SET log_min_messages = 'debug3';
```

## Example Log Output

```
DEBUG:  Custom autovacuum policy: high accumulation rate (0.25 dead tuples/sec), aggressive vacuum
DEBUG3: Custom autovacuum policy: dead_tuples=1500, time_since_vacuum=6000000 ms, rate=0.25/sec, base_thresh=1000, adj_thresh=750, error=0.15
```

## Limitations

- Only one policy can be active (the proportional controller)
- Policies cannot override wraparound vacuum requirements

## Troubleshooting

1. **Extension not loading**: Check `shared_preload_libraries` in postgresql.conf
2. **Policy not working**: Verify the extension is created in the database
3. **No debug output**: Set `log_min_messages = 'debug2'` and restart

## Customization

To modify the policy behavior, edit the `custom_autovacuum_policy()` function in `custom_autovacuum.c`:

```c
/* Proportional controller parameters */
kp = 0.1;  /* Proportional gain - adjust this to control sensitivity */
target_rate = 0.1;  /* Target dead tuple rate (dead tuples per second) */
min_threshold = 50.0;  /* Minimum threshold */
max_threshold = base_threshold * 3.0;  /* Maximum threshold */
```

## Contributing

To modify the policy:

1. Edit the `custom_autovacuum_policy()` function in `custom_autovacuum.c`
2. Rebuild and reinstall the extension
3. Restart PostgreSQL
4. Test thoroughly with various table sizes and update patterns