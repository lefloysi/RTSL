# Status: PLANNING

Allowed statuses: `IDLE`, `PLANNING`, `IN_PROGRESS`, `VERIFYING`, `BLOCKED`,
`DONE`.

State flow:

```text
IDLE -> PLANNING -> IN_PROGRESS -> VERIFYING -> DONE
                         |
                         -> BLOCKED
```

`DONE` is allowed only when there are no unchecked checklist items, all
required verification commands have results, final repository-wide search is
recorded, blockers are empty, and continuation notes summarize the resulting
architecture.

## Objective

## Architectural Rule

## Documentation Consulted

## Impact Map

### Confirmed Violations

### Suspicious Related Locations

### Inspected And Ruled Out

## Invariants

### Before

### After

## Checklist

## Verification

### Commands

### Results

## Final Search

## Blockers

## Continuation Notes
