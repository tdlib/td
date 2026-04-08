pyJA3
=====
.. image:: https://readthedocs.org/projects/pyja3/badge/?version=latest
    :target: http://pyja3.readthedocs.io/en/latest/?badge=latest

.. image:: https://badge.fury.io/py/pyja3.svg
    :target: https://badge.fury.io/py/pyja3


JA3 provides fingerprinting services on SSL packets. This is a python wrapper around JA3 logic in order to produce valid JA3 fingerprints from an input PCAP file.


Getting Started
---------------
1. Install the pyja3 module:

    ``pip install pyja3`` or ``python setup.py install``

2. Test with a PCAP file or download a sample:

    $(venv) ja3 --json /your/file.pcap

Example
-------
Output from sample PCAP::

    [
        {
            "destination_ip": "192.168.1.3",
            "destination_port": 443,
            "ja3": "769,255-49162-49172-136-135-57-56-49167-49157-132-53-49159-49161-49169-49171-69-68-51-50-49164-49166-49154-49156-150-65-4-5-47-49160-49170-22-19-49165-49155-65279-10,0-10-11-35,23-24-25,0",
            "ja3_digest": "2aef69b4ba1938c3a400de4188743185",
            "source_ip": "192.168.1.4",
            "source_port": 2061,
            "timestamp": 1350802591.754299
        },
        {
            "destination_ip": "192.168.1.3",
            "destination_port": 443,
            "ja3": "769,255-49162-49172-136-135-57-56-49167-49157-132-53-49159-49161-49169-49171-69-68-51-50-49164-49166-49154-49156-150-65-4-5-47-49160-49170-22-19-49165-49155-65279-10,0-10-11-35,23-24-25,0",
            "ja3_digest": "2aef69b4ba1938c3a400de4188743185",
            "source_ip": "192.168.1.4",
            "source_port": 2068,
            "timestamp": 1350802597.517011
        }
    ]

Changelog
---------
2018-02-05
~~~~~~~~~~
* Change: Ported single script to valid Python Package
* Change: Re-factored code to be cleaner and PEP8 compliant
* Change: Supported Python2 and Python3

