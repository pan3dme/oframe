import importlib.util
print({m: bool(importlib.util.find_spec(m)) for m in ('unicorn', 'elftools', 'cffi')})
