#!/usr/bin/env python
import os
from setuptools import setup, find_packages


def read(fname):
    return open(os.path.join(os.path.dirname(__file__), fname)).read()

setup(
    name='pyja3',
    version='1.1.0',
    description='Generate JA3 fingerprints from PCAPs using Python.',
    url="https://github.com/salesforce/ja3",
    author="Tommy Stallings",
    author_email="tommy.stallings2@gmail.com",
    maintainer = "John B. Althouse",
    maintainer_email = "jalthouse@salesforce.com",
    license="BSD",
    packages=find_packages(),
    install_requires=['dpkt'],
    long_description=read('README.rst'),
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: End Users/Desktop',
        'License :: OSI Approved :: BSD License',
        'Natural Language :: English',
        'Programming Language :: Python',
        'Topic :: Software Development :: Libraries'
    ],
    package_data={
        'pyja3': [],
    },
    entry_points={
        'console_scripts': [
            'ja3 = ja3.ja3:main'
        ]
    },
    keywords=['ja3', 'fingerprints', 'defender', 'ssl', 'packets']
)
