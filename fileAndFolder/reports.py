import datetime
import logging
import PySimpleGUI as gui
from logging.handlers import RotatingFileHandler

#TODO: shift log file config to file
logFileName = 'logs_duplicateFinder.log'
loggingLevel = logging.INFO
logging.basicConfig(level=loggingLevel, format='%(levelname)s %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S') #outputs to console
#log = logging.getLogger(__name__)
handler = RotatingFileHandler(logFileName, maxBytes=2000000, backupCount=2)#TODO: Shift to config file
handler.formatter = logging.Formatter(fmt='%(levelname)s %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S') #setting this was necessary to get it to write the format to file. Without this, only the message part of the log would get written to the file
logging.getLogger().addHandler(handler)
logging.getLogger().setLevel(loggingLevel)

#-----------------------------------------------             
#-----------------------------------------------
#---------------- HELPER CLASSES  --------------
#-----------------------------------------------
#-----------------------------------------------        
class Reports:
    def __init__(self, folderToStore, fileOps):
        self.fileOps = fileOps
        self.folderToStore = folderToStore
        self.report = []
        self.reportFilenameWithPath = 'None'
    
    def add(self, text):
        self.report.append(text)
        
    def generateReport(self, shouldWriteReportToFile = False):
        for aLine in self.report:
            logging.info(aLine)
        if shouldWriteReportToFile:
            self.reportFilenameWithPath = self.folderToStore + "Report_" + str(datetime.datetime.now()) + ".txt"
            self.fileOps.writeLinesToFile(self.reportFilenameWithPath, self.report)
        gui.popup('Completed. Report: ' + self.reportFilenameWithPath, keep_on_top=True)            
        