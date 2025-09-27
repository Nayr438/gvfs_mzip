# GunZ MRS File Viewer.

This tool can view and decompress multiple MRS Versions, it can not however compress them as that is not the goal of this project.

Requires Meson to build.
Requires QT for the GUI Build, would have preferred to use something else but cross-platform toolkits kind of suck. 
Given that I still have to jump through hoops to get QT Builds on Windows I may look at alternatives.

MZipRecovery attempts to bruteforce decompression of archives that utilize ConvertChar/RecoveryChar, it should automatically attempt this if the signature check fails while using the GUI build.

If some things seem redundant, it's because MZip was initially being designed as a library.

A demo from before the rewrite, but this one works similarly.

[![Youtube Video]()](https://www.youtube.com/watch?v=Cs0duLUxQew)
