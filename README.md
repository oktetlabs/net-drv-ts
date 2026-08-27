# NET-DRV-TS - testing network interface driver

NetDrv TS carries out testing and performance evaluation of kernel network drivers.

The test suite covers (list not exhaustive):

* basic operations, e.g. querying driver info, changing MAC or MTU, sending pings, etc;
* different kinds of offloading: TCO, LCO, GRO, etc;
* compatibility with ethtool;
* devlink API.

TE is used as an Engine that allows to prepare desired environment for every test.
This guarantees reproducible test results.

The following setup is used to run the tests:

![Testbed](net-drv-ts/doc/dox_resources/images/testbed.png)

## Documentation

The suite documentation is generated with doxygen and doxyrest:

```
export DOXYREST_PREFIX=/path/to/doxyrest-<version>-linux-amd64
./scripts/gen_doxygen --html
```

Run `$TE_BASE/scripts/doxyrest_deploy.sh --put` first if doxyrest is not
installed; `--how` prints what it would do without doing it.

Test scenarios are read out of the C sources with libclang, so the
Python bindings are needed to document them properly:

```
apt install python3-clang     # or: pip install libclang
```

The build still succeeds without them, but every scenario comes out
flat: steps lose the conditions and loops guarding them, and substeps do
not nest.
