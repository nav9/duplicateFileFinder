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

class FileDuplicateSearchBinaryMode:
    def __init__(self, foldername, fileOps):
        self.fileOps = fileOps
        self.baseFolder = foldername
        self.folderForDuplicateFiles = self.baseFolder + const.GlobalConstants.duplicateFilesFolder        
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.reports = reports.Reports(self.folderForDuplicateFiles, self.fileOps)        
        self.reports.add('Searching in : ' + self.baseFolder)
        self.reports.add('Duplicates will be stored in: ' + self.folderForDuplicateFiles)
        self.undoStore = undo.Undo(self.folderForDuplicateFiles, self.fileOps)
        self.atLeastOneDuplicateFound = False      
    
    def search(self):        
        firstDuplicate = False        
        #---initiate search for duplicates
        totalFolders = len(self.folderPaths)
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            if path == self.folderForDuplicateFiles:#don't search an existing duplicates folder
                continue
            logging.info('Searching in ' + str(path))
            totalFilesInFolder = len(filenames)               
            for fileOrdinal in range(len(filenames)):#for each file in the folder
                duplicateOrdinal = 0
                filesize = self.fileSizes[folderOrdinal][fileOrdinal]
                filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                if filename == const.GlobalConstants.alreadyProcessedFile:
                    continue
                logging.info("Processing file " + str(fileOrdinal) + "/" + str(totalFilesInFolder) + " in folder " + str(folderOrdinal) + "/" + str(totalFolders))
                #---compare with all files
                for folderOrdinalToCompare in range(len(self.folderPaths)):#for each folder
                    filenamesToCompare = self.filesInFolder[folderOrdinalToCompare]
                    pathToCompare = self.folderPaths[folderOrdinalToCompare] 
                    if pathToCompare == self.folderForDuplicateFiles:#don't search an existing duplicates folder
                        continue                     
                    for fileOrdinalToCompare in range(len(filenamesToCompare)):#for each file in the folder
                        filenameToCompare = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
                        if folderOrdinal == folderOrdinalToCompare and fileOrdinal == fileOrdinalToCompare:#skip self
                            continue
                        if filenameToCompare == const.GlobalConstants.alreadyProcessedFile:
                            continue                    
                        filesizeToCompare = self.fileSizes[folderOrdinalToCompare][fileOrdinalToCompare]
                        if filesize == filesizeToCompare:#initial match found based on size
                                                                                   
                            #---now compare based on file contents
                            filesAreSame = self.fileOps.compareEntireFiles(path + filename, pathToCompare + filenameToCompare)
                            if filesAreSame:
                                if not firstDuplicate:
                                    firstDuplicate = True
                                    self.fileOps.createDirectoryIfNotExisting(self.folderForDuplicateFiles)
                                self.atLeastOneDuplicateFound = True
                                duplicateOrdinal = duplicateOrdinal + 1
                                self.__moveFileToSeparateFolder__(folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare, duplicateOrdinal)
                                self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal)
        if self.atLeastOneDuplicateFound:
            self.undoStore.generateUndoFile()
        else:
            self.reports.add("No duplicates found")
        self.reports.generateReport(self.atLeastOneDuplicateFound)
    
    def __moveFileToSeparateFolder__(self, folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare, duplicateOrdinal):
        #Note: Empty files will be identified as duplicates of other empty files. It's normal.  
        #TODO: if move not possible, copy and mention any move issues in the generated report
        folder = self.folderPaths[folderOrdinal]
        file = self.filesInFolder[folderOrdinal][fileOrdinal]        
        dupFolder = self.folderPaths[folderOrdinalToCompare]
        dupFile = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
        #TODO: check if string length is appropriate for the filesystem        
        fileName, fileExtension = self.fileOps.getFilenameAndExtension(file)
        #TODO: can have a try catch to check if directory exists, before doing the move (in case the directory gets deleted during runtime)
        newFilename = fileName + "_" + str(duplicateOrdinal) + fileExtension
        self.fileOps.moveFile(dupFolder, dupFile, self.folderForDuplicateFiles, newFilename) 
        reportString = folder + file + "'s duplicate: " + dupFolder + dupFile + " is renamed and moved to " + self.folderForDuplicateFiles
        self.reports.add(reportString)
        self.undoStore.add(dupFolder, dupFile, self.folderForDuplicateFiles, newFilename)
    
    def __markAlreadyProcessedFile__(self, folderOrdinal, fileOrdinal):
        self.filesInFolder[folderOrdinal][fileOrdinal] = const.GlobalConstants.alreadyProcessedFile            
