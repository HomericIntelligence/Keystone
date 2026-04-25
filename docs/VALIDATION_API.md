# Validation API Reference

## validate_id

The `validate_id` function validates ID strings for safe use in URL path segments.

### Signature

```python
def validate_id(value: str, field_name: str) -> str
```

### Rejection Criteria

The function rejects strings that:

1. **Empty or whitespace-only**: Must contain at least one non-whitespace character
2. **Exceeds 256 characters**: Maximum length is 256 characters
3. **Contains whitespace or control characters**: Any space, tab, newline, or ASCII character with code < 32 is rejected
4. **Contains path separators**: Forward slash `/` or backslash `\` characters are rejected
5. **Contains path traversal sequences**: The substring `..` is rejected to prevent directory traversal

### Parameters

- `value` (str): The ID string to validate
- `field_name` (str): Human-readable field name for error messages

### Returns

- str: The original `value` unchanged if all validation checks pass

### Raises

- `ValueError`: If the input fails any validation check. The error message
  includes the field name and description of the failure.

### Examples

```python
# Valid IDs
validate_id("task-123", "task_id")       # OK
validate_id("agent_foo_bar", "agent_id") # OK
validate_id("msg.event.type", "subject") # OK

# Invalid IDs
validate_id("", "id")                    # ValueError: empty
validate_id("task/123", "id")            # ValueError: path separator
validate_id("../etc/passwd", "id")       # ValueError: path traversal
validate_id("task 123", "id")            # ValueError: whitespace
validate_id("x" * 257, "id")             # ValueError: too long
```
