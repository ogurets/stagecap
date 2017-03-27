# stagecap
Command line camera capture based on stagefright api (mostly).

Very raw stuff, but enough to get the idea.

Building: clone this into android source repo, path "frameworks/base/cmds" and run "make -j8 stagecap". It builds as standalone cli utility.

Usage: upload compiled binary to device and run "stagecap camera 1". Should work on any Android, but I've tested only on version 2.3.

Funny thing - looks like it requires drawing an overlay on-screen to work. Stealthy operation was not among my goals, so I didn't try too hard to resolve this little inconvenience.
