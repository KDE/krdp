# Frame statistics

To get frame statistics run the following:

QT_LOGGING_RULES="kpipewire_frame_tracking.info=true;org.kde.krdp.frame_tracking=true"  krdpserver 2> my_log

./frame-log-to-csv.py my_log > out.csv

xdg-open visuals.ods

the spreadsheet contains a "linked resource" to out.csv file.

You may need to adjust the data range of the graph as appropriate to cover the captured area.
