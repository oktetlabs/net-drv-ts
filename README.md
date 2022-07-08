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
