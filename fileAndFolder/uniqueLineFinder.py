import os
import mimetypes
from fileAndFolder import reports
from fileAndFolder import undo
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

class UniqueLineFinder:
    def __init__(self, foldername, fileOps):
        self.fileOps = fileOps
        self.baseFolder = foldername
        self.folderForConsolidatedFile = self.baseFolder + const.GlobalConstants.duplicateLinesFolder      
        self.fileToStoreUniqueSentences = os.path.join(self.folderForConsolidatedFile, const.GlobalConstants.uniqueLinesFilename)  
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.reports = reports.Reports(self.folderForConsolidatedFile, self.fileOps)        
        self.reports.add('Searching in : ' + self.baseFolder)
        self.reports.add('File containing unique sentences will be stored in: ' + self.folderForConsolidatedFile)   
        self.switchedOffGUI = False
        self.uniqueSentences = dict() #Can use set for faster processing, but order will not be preserved in set. From Python 3.7 onward, order is preserved for dict https://stackoverflow.com/questions/39980323/are-dictionaries-ordered-in-python-3-6
        self.numberOfDuplicateSentencesFound = 0
    
    def search(self): #TODO: This function needs to be a lot more modular to be reusable  
        self.numberOfDuplicateSentencesFound = 0
        #---initiate search for duplicates
        totalFolders = len(self.folderPaths)
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            if path == self.folderForConsolidatedFile:#don't search an existing duplicates folder
                continue
            logging.info('Searching in ' + str(path))
            totalFilesInFolder = len(filenames)               
            for fileOrdinal in range(len(filenames)):#for each file in the folder
                #filesize = self.fileSizes[folderOrdinal][fileOrdinal]
                filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                filenameWithPath = os.path.join(path, filename)
                #if self.isNotTextFile(filenameWithPath):#skipping files that aren't text files
                #    logging.info("Skipping non-text file " + filenameWithPath)
                #    continue
                logging.info("Processing file " + str(fileOrdinal+1) + "/" + str(totalFilesInFolder) + " in folder " + str(folderOrdinal+1) + "/" + str(totalFolders))                
                #---read the lines from the file
                gen = self.fileOps.getGeneratorObjectToFileForReadingLines(filenameWithPath)
                lines = self.fileOps.readBunchOfLinesFromTextFile(gen)
                if lines:
                    self.store(lines)
                else:
                    break #end of file reached. Move on to the next folder           
                
        if self.numberOfUniqueLines() > 0:
            self.reports.add("Found " + str(self.numberOfUniqueLines()) + " unique lines and " + str(self.numberOfDuplicateSentencesFound) + " duplicates.")            
            self.saveLinesToFile()
            self.reports.add("Consolidated lines stored in: " + self.folderForConsolidatedFile)
        else:
            self.reports.add("No duplicates found, but " + str(self.numberOfUniqueLines()) + " unique lines are consolidated into " + self.folderForConsolidatedFile)
        if not self.switchedOffGUI: #TODO: need to implement this in a more modular and configurable/scalable way
            self.reports.generateReport(self.wereDuplicatesFound())                
    
    def saveLinesToFile(self):
        self.fileOps.createDirectoryIfNotExisting(self.folderForConsolidatedFile)
        self.fileOps.writeKeysOfDictToFile(self.fileToStoreUniqueSentences, self.uniqueSentences)
        
    def store(self, lines):
        for line in lines:
            line = line.strip() #removes leading and trailing spaces
            if line in self.uniqueSentences:
                self.numberOfDuplicateSentencesFound += 1
            else:
                self.uniqueSentences[line] = None
                
    def numberOfUniqueLines(self):
        return len(self.uniqueSentences.keys())
        
    # def isNotTextFile(self, filenameWithPath):
    #     isTextFile = False
    #     typeInfo = mimetypes.guess_type(filenameWithPath) #The output will be like ('text/x-sh', None) or ('text/x-python', None) or ('application/octet-stream', None) and so on       
    #     if typeInfo[const.GlobalConstants.FIRST_POSITION_IN_LIST].startswith(const.GlobalConstants.TEXT_FILE_MIME_TYPE):
    #         isTextFile = True
    #     return isTextFile
        
    def switchOffGUI(self): #Used when running test cases
        self.switchedOffGUI = True
        
    def wereDuplicatesFound(self):
        duplicatesFound = False
        if self.numberOfUniqueLines() > 0:
            duplicatesFound = True
        return duplicatesFound
    
    def howManyDuplicatesFound(self):
        return self.numberOfDuplicateSentencesFound
    
    def getFolderToStoreConsolidatedFile(self):
        return self.folderForConsolidatedFile