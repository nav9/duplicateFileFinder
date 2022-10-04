import os
import shutil #for moving file
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

#-----------------------------------------------             
#-----------------------------------------------
#------------------ FILE OPS -------------------
#-----------------------------------------------
#-----------------------------------------------

class FileOperations:
    def __init__(self):
        self.FULL_FOLDER_PATH = 0
        #self.SUBDIRECTORIES = 1
        self.FILES_IN_FOLDER = 2
        self.CHUNK_SIZE_FOR_BINARY_FILE_COMPARISON = 8 * 1024     
    
    """ Get names of files in each folder and subfolder. Also get sizes of files """
    def getFileNamesOfFilesInAllFoldersAndSubfolders(self, folderToConsider): 
        #TODO: read-only files, files without permission to read, files that can't be moved, corrupted files
        #TODO: What about "file-like objects"
        folderPaths = []; filesInFolder = []; fileSizes = []
        logging.info("Obtaining a list of folders and files...")
        result = os.walk(folderToConsider, followlinks=False) #Won't throw an error if folder does not exist. followlinks=False allows skipping symlinks. It's False by default. Just making it obvious here
        logging.info("Walking to collect names of files in this folder: " + str(folderToConsider))
        for oneFolder in result:
            folderPath = self.folderSlash(oneFolder[self.FULL_FOLDER_PATH])
            folderPaths.append(folderPath)
            #subdir = oneFolder[self.SUBDIRECTORIES]
            filesInThisFolder = oneFolder[self.FILES_IN_FOLDER]
            filesNotFound = []
            sizeOfFiles = []
            for filename in filesInThisFolder:
                try:
                    fileProperties = os.stat(folderPath + filename)
                    sizeOfFiles.append(fileProperties.st_size)
                except FileNotFoundError:
                    filesNotFound.append(filename)
                    pass #ignore files that are not found. It may be a broken symlink or a file that was deleted in-between or a file on a connected device that got disconnected
            filesInThisFolder = [filename for filename in filesInThisFolder if filename not in filesNotFound] #remove any files not found, from the list
            fileSizes.append(sizeOfFiles)
            filesInFolder.append(filesInThisFolder)            
        return folderPaths, filesInFolder, fileSizes #returns as [fullFolderPath1, fullFolderPath2, ...], [[filename1, filename2, filename3, ...], [], []], [[filesize1, filesize2, filesize3, ...], [], []]
   
    def isValidFile(self, filenameWithPath):
        return os.path.isfile(filenameWithPath)   
    
    def getFilenameAndExtension(self, filenameOrPathWithFilename):
        filename, fileExtension = os.path.splitext(filenameOrPathWithFilename)
        return filename, fileExtension
    
    def deleteFile(self, filenameWithPath):
        os.remove(filenameWithPath) #TODO: check if file exists before deleting
        
    def writeLinesToFile(self, filenameWithPath, report):
        fileHandle = open(filenameWithPath, 'w')
        for line in report:
            fileHandle.write(line)
            fileHandle.write("\n")
        fileHandle.close()
        
    def readFromFile(self, filenameWithPath):
        with open(filenameWithPath) as f:
            lines = f.read().splitlines()#TODO: try catch
        return lines              
    
    def createDirectoryIfNotExisting(self, folder):
        if not os.path.exists(folder): 
            try: os.makedirs(folder)
            except FileExistsError:#in case there's a race condition where some other process creates the directory before makedirs is called
                pass
            
    def getCurrentDirectory(self):
        return os.getcwd()
    
    def isThisValidDirectory(self, folderpath):
        return os.path.exists(folderpath)
    
    def deleteFolderIfItExists(self, folderPath):
        try:
            if os.path.exists(folderPath):
                shutil.rmtree(folderPath, ignore_errors = True) #The ignore_errors is for when the folder has read-only files https://stackoverflow.com/a/303225/453673
        except Exception as e:
            logging.error("Error when deleting folder: " + folderPath + ". Exception: " + str(e))

    """ Move file to another directory. Renaming while moving is possible """
    def moveFile(self, existingPath, existingFilename, newPath, newFilename):
        try:
            shutil.move(existingPath + existingFilename, newPath + newFilename)    
        except FileNotFoundError:
            logging.error("Could not find file: " + existingPath + existingFilename + " when trying to move it to " + newPath + newFilename)

    def copyFile(self, filename, folderToCopyInto):
        try:
            shutil.copy(filename, folderToCopyInto)
        except FileNotFoundError:
            logging.error("Could not find file: " + filename + " or folder " + folderToCopyInto)    
    
    """ Adds a slash at the end of the folder name if it isn't already present """
    def folderSlash(self, folderName):
        return os.path.join(folderName, "") #https://stackoverflow.com/questions/2736144/python-add-trailing-slash-to-directory-string-os-independently

    """ Binary comparison is performed chunk by chunk. If any chunk mismatches, the comparison is stopped and the function returns """
    def compareEntireFiles(self, filename1, filename2):
        try:
            with open(filename1, 'rb') as filePointer1, open(filename2, 'rb') as filePointer2:
                while True:
                    chunk1 = filePointer1.read(self.CHUNK_SIZE_FOR_BINARY_FILE_COMPARISON)#TODO: try catch
                    chunk2 = filePointer2.read(self.CHUNK_SIZE_FOR_BINARY_FILE_COMPARISON)
                    if chunk1 != chunk2:
                        return False
                    if not chunk1:#if chunk is of zero bytes (nothing more to read from file), return True because chunk2 will also be zero. If it wasn't zero, the previous if would've been False
                        return True       
        except FileNotFoundError:
            logging.error("One of these files had a FileNotFoundError. Skipping: " + filename1 + " or " + filename2) #the file is not found either because it's a broken link or the file does not actually exist
    
    def generateFileWithRandomData(self, filenameWithPath, fileSize):
        with open(filenameWithPath, 'wb') as fileHandle:
            fileHandle.write(os.urandom(fileSize))    
            