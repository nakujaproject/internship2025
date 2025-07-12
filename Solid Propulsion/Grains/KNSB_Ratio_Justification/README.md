# KNSB Parameter Automation

## Introduction

We found it difficult to keep on having to input manually ProPEP's output to an Excel sheet. Also, ProPEP didn't work on my computer as it kept crashing throughout. This meant that I needed to make my own version of this calculator. I'll be employing some interesting libraries. My supervisor suggested that we build our own utilities, that's how you learn better.

## Aims of this Software

* To simulate composite propellant compositions and obtain burn rate exponents and other important information.
* To ease the analysis of the information obtained from the simulation.
* To develop mathematical model that justifies a required grain geometry 

## Background information

ProPEP software was originally written in Fortran for the IBM z/OS in the 1970s and later in 1989 by Martin Marietta. There exists a version of [PEP for z/OS](https://github.com/haynieresearch/pep-for-zos) that was rewritten in 2017 by Lance Haynie (*victim of copypasta*), for the same old OS. We're in 2025 right now. This is so frustrating. It's still written in Fortran vesion 2. So many `GOTO 9999`s are in this code that it breaks my heart. This is why I'm doing this in good old reliable and simple Python.

## 1. 