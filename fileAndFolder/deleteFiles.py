import fnmatch #for matching wildcards
from fileAndFolder import reports
from programConstants import constants as const
import logging
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

class FileSearchDeleteSpecifiedFiles:
    def __init__(self, foldername, fileOps):
        self.fileOps = fileOps
        self.baseFolder = foldername
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.reports = reports.Reports(self.baseFolder, fileOps)
        self.reports.add('Searching in: ' + self.baseFolder)
        self.atLeastOneFileFound = False
    
    def searchAndDestroy(self, filesToDelete, caseSensitive):
        self.reports.add("Files to delete: " + str(filesToDelete))
        
        #---initiate search for duplicates
        totalFolders = len(self.folderPaths)
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            #filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            logging.info('Processing folder' + str(folderOrdinal) + '/' + str(totalFolders) + '. Searching in ' + str(path))
            if not caseSensitive:
                filesToDelete = [x.lower() for x in filesToDelete]
            filesToDelete = [x.strip() for x in filesToDelete]
            logging.debug('filesToDelete:' + str(filesToDelete))
            for theFileToDelete in filesToDelete:
                logging.debug("Searching for files/pattern: " + str(theFileToDelete))
                
                filenameMatchingMode = const.FilenameMatching.fullString #the default for an exact filename match         
                if "*" in theFileToDelete:#TODO: May need checks to verify if the wildcard pattern is ok
                    filenameMatchingMode = const.FilenameMatching.wildcard
                
                for fileOrdinal in range(len(self.filesInFolder[folderOrdinal])):#for each file in the selected folder
                    filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                    if not caseSensitive:
                        filename = filename.lower()
                    #---what type of filename comparison?
                    deleteIt = False
                    if filenameMatchingMode == const.FilenameMatching.wildcard:
                        if fnmatch.fnmatch(filename, theFileToDelete): #matched with wildcard *
                            deleteIt = True                        
                    if filenameMatchingMode == const.FilenameMatching.fullString:
                        if filename.strip() == theFileToDelete: #exact match
                            deleteIt = True
                    if deleteIt:
                        self.__deleteFile__(folderOrdinal, fileOrdinal)
        if not self.atLeastOneFileFound:
            self.reports.add("No files found")
        self.reports.generateReport(self.atLeastOneFileFound)
    
    def __deleteFile__(self, folderOrdinal, fileOrdinal):  
        #TODO: if delete not possible, mention it in the generated report and don't make atLeastOneFileFound true
        folder = self.folderPaths[folderOrdinal]
        file = self.filesInFolder[folderOrdinal][fileOrdinal]        
        self.fileOps.deleteFile(folder + file)
        reportString = "Deleted " + folder + file
        self.reports.add(reportString)
        self.atLeastOneFileFound = True
