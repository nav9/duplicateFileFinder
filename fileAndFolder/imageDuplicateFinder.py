import os
import imagehash
from PIL import Image
from programConstants import constants as const
from fileAndFolder import reports
from fileAndFolder import undo
from PIL import UnidentifiedImageError
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

class ImageHashComparison:
    def __init__(self):
        self.hash_size = 8 #default value is 8. Can be increased to 24 to improve similarity detection accuracy
    
    def compareIfExactlySame(self, image, imageToCompare):
        imagesAreSame = False
        try:
            #Note: average_hash is less accurate at detecting similarities than phash, but phash takes a tiny bit longer to generate the hash
            #hash1 = imagehash.average_hash(Image.open(image), self.hash_size)
            #hash2 = imagehash.average_hash(Image.open(imageToCompare), self.hash_size)
            hash1 = imagehash.phash(Image.open(image), self.hash_size)
            hash2 = imagehash.phash(Image.open(imageToCompare), self.hash_size)
            if hash1 == hash2:
                imagesAreSame = True
        except UnidentifiedImageError:
            logging.error("Skipping, since one of these files is not an image: " + image + " or " + imageToCompare)
        return imagesAreSame

class ImageDuplicateSearch:
    def __init__(self, foldername, fileOps):
        self.fileOps = fileOps
        self.baseFolder = foldername
        self.folderForDuplicateFiles = self.baseFolder + const.GlobalConstants.duplicateImagesFolder        
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.reports = reports.Reports(self.folderForDuplicateFiles, self.fileOps)
        self.reports.add('Searching in : ' + self.baseFolder)
        self.reports.add('Duplicates will be stored in: ' + self.folderForDuplicateFiles)
        self.atLeastOneDuplicateFound = False
        self.searchWithoutMovingFiles = False 
        self.switchedOffGUI = False        
        self.undoStore = undo.Undo(self.folderForDuplicateFiles, self.fileOps)
        self.imageComparison = ImageHashComparison()        
    
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
                #filesize = self.fileSizes[folderOrdinal][fileOrdinal]
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
                        _, fileExtension = os.path.splitext(filenameToCompare)
                        if len(fileExtension) == 0:
                            self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                            continue
                        else:
                            fileExtension = fileExtension[1:]#ignores the dot in '.jpg'
                        if fileExtension.lower() not in const.GlobalConstants.supportedImageFormats:#file is not an image or is not a supported image format
                            self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                            continue                                                                                   
                        #---now compare
                        #TODO: before sending the files for comparison, check if it is a supported image file https://stackoverflow.com/questions/889333/how-to-check-if-a-file-is-a-valid-image-file
                        filesAreSame = self.imageComparison.compareIfExactlySame(path + filename, pathToCompare + filenameToCompare)
                        if filesAreSame:
                            if not firstDuplicate:
                                firstDuplicate = True
                                self.fileOps.createDirectoryIfNotExisting(self.folderForDuplicateFiles)
                            self.atLeastOneDuplicateFound = True
                            duplicateOrdinal = duplicateOrdinal + 1
                            if not self.searchWithoutMovingFiles:
                                self.__moveFileToSeparateFolder__(folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare, duplicateOrdinal)
                            self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal)
        if self.atLeastOneDuplicateFound:
            self.undoStore.generateUndoFile()
        else:
            self.reports.add("No duplicates found")
        if not self.switchedOffGUI: #TODO: need to implement this in a more modular and configurable/scalable way
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

    def setToSearchWithoutMovingFile(self): #currently used only for testing, but can be integrated into GUI using a checkbox
        self.searchWithoutMovingFiles = True 
        
    def setToSearchByMovingFile(self):
        self.searchWithoutMovingFiles = False
        
    def switchOffGUI(self): #Used when running test cases
        self.switchedOffGUI = True
        
    def wereDuplicatesFound(self):
        return self.atLeastOneDuplicateFound