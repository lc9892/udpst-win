# UDP Speed Test for Windows

The **udpst-win** application utilizes core components from the formal Open
Broadband - UDP Speed Test (**OB-UDPST**) Linux software release, sponsored by
the [**Broadband Forum**](https://www.broadband-forum.org/). The OB-UDPST
[project page](https://broadband-forum.atlassian.net/wiki/spaces/BBF/pages/46640109/Open+Broadband-UDP+Speed+Test+OB-UDPST) and public
[software mirror](https://github.com/BroadbandForum/obudpst) are available
for additional details.

## Software Architecture

This application is built using Microsoft Visual Studio, specifically the 2026
Community (free) edition. The OB-UDPST respository source code is copied into
a project subdirectory called "udpst". Although native Windows code was created to
handle Windows-specific events (the GUI, timers, I/O notifications), all of the 
protocol handling and actual IP capacity testing is done by the core OB-UDPST
software. No modification of the original OB-UDPST source is required.

*Note: The OB-UDPST version included with this project under "udpst" may be
newer than the released version found at the mirror listed above. If so, the latest
release candidate is available via the BBF Bitbucket repository available to project
members. As a reminder, a newer version of the server will be backward compatible with
clients at least one version behind.*
