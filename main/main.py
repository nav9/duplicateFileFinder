'''
File duplicate finder. Special mode for comparing images too.
Created on 19-Feb-2021
@author: Navin
'''

from __future__ import print_function   # py2 compatibility
import PySimpleGUI as sg
import os
import shutil #for moving file
#import filetype
#import Image

class GlobalConstants:
    duplicateFilesFolder = "duplicateFilesFolder/"
    #duplicateImagesFolder = "duplicateImagesFolder/"
    EVENT_CANCEL = 'Cancel'
    EVENT_EXIT = 'Cancel'
    alreadyProcessedFile = "."

class FileOperations:
    def __init__(self):
        self.FULL_FOLDER_PATH = 0
        #self.SUBDIRECTORIES = 1
        self.FILES_IN_FOLDER = 2
    
    """ Get names of files in each folder and subfolder. Also get sizes of files """
    def getFileNamesOfFilesInAllFoldersAndSubfolders(self, folderToConsider): 
        #TODO: check if folder exists
        #TODO: what about symlinks?
        #TODO: read-only files, files without permission to read, files that can't be moved, corrupted files
        #TODO: What about "file-like objects"
        #TODO: Encrypted containers?
        folderPaths = []; filesInFolder = []; fileSizes = []
        result = os.walk(folderToConsider)        
        for oneFolder in result:
            folderPath = self.folderSlash(oneFolder[self.FULL_FOLDER_PATH])
            folderPaths.append(folderPath)
            #subdir = oneFolder[self.SUBDIRECTORIES]
            filesInThisFolder = oneFolder[self.FILES_IN_FOLDER]
            sizeOfFiles = []
            for filename in filesInThisFolder:
                fileProperties = os.stat(folderPath + filename)
                sizeOfFiles.append(fileProperties.st_size)
            fileSizes.append(sizeOfFiles)
            filesInFolder.append(filesInThisFolder)            
        return folderPaths, filesInFolder, fileSizes #returns as [fullFolderPath1, fullFolderPath2, ...], [[filename1, filename2, filename3, ...], [], []], [[filesize1, filesize2, filesize3, ...], [], []]
   
    def isValidFile(self, filenameWithPath):
        return os.path.isfile(filenameWithPath)   
    
    def getFilenameAndExtension(self, filenameOrPathWithFilename):
        filename, fileExtension = os.path.splitext(filenameOrPathWithFilename)
        return filename, fileExtension
    
    def deleteFile(self, folder, file):
        os.remove(folder + file) #TODO: check if file exists before deleting
    
    def createDirectoryIfNotExisting(self, folder):
        if not os.path.exists(folder): 
            try: os.makedirs(folder)
            except FileExistsError:#in case there's a race condition where some other process creates the directory before makedirs is called
                pass      
            
    def isThisValidDirectory(self, folderpath):
        return os.path.exists(folderpath)

    """ Move file to another directory. Renaming while moving is possible """
    def moveFile(self, existingPath, existingFilename, newPath, newFilename):
        shutil.move(existingPath + existingFilename, newPath + newFilename)    
    
    """ Adds a slash at the end of the folder name if it isn't already present """
    def folderSlash(self, folderName):
        if folderName.endswith('/') == False: 
            folderName = folderName + '/' 
        return folderName    

class FileSearchModes:
    choice_None = 'Exit'
    choice_fileBinary = 'Duplicate files (byte search)'
    #choice_imagePixels = 'Duplicate images (pixel search)'    
    choice_residualFiles = 'Delete residual files (like Thumbs.db etc.)'
    
class FirstChoiceMenu:
    def __init__(self):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35       
    
    def showUserTheMenu(self):
        #---choose mode of running        
        layout = [
                    [sg.Text('What kind of search do you want to do?')],
                    #[sg.Combo([FileSearchModes.choice_fileBinary, FileSearchModes.choice_imagePixels, FileSearchModes.choice_residualFiles], default_value=FileSearchModes.choice_residualFiles)],
                    [sg.Combo([FileSearchModes.choice_fileBinary, FileSearchModes.choice_residualFiles], default_value=FileSearchModes.choice_fileBinary)],        
                    [sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')],
                    [sg.Button(GlobalConstants.EVENT_CANCEL), sg.OK()]
                 ]
        window = sg.Window('', layout, element_justification='right', grab_anywhere=True)    
        self.event, self.values = window.read()     
        window.close()
        
    def getUserChoice(self):
        retVal = None
        if self.event == sg.WIN_CLOSED or self.event == GlobalConstants.EVENT_EXIT or self.event == GlobalConstants.EVENT_CANCEL:
            exit()
            #retVal = FileSearchModes.choice_None
        else:
            retVal = self.values[0]    
        return retVal #returns one of the FileSearchModes

class FolderChoiceMenu:
    def __init__(self):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35       
    
    def showUserTheMenu(self, topText, bottomText):
        #---choose mode of running
        layout = []
        for s in topText:
            layout.append([sg.Text(s, justification='left')])
        layout.append([sg.Input(), sg.FolderBrowse()])
        for s in bottomText:
            layout.append([sg.Text(s, text_color='grey', justification='left')])        
        layout.append([sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')])
        layout.append([sg.Button(GlobalConstants.EVENT_CANCEL), sg.Button('Ok')])
        
        window = sg.Window('', layout, grab_anywhere=False, element_justification='right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        retVal = None
        if self.event == sg.WIN_CLOSED or self.event == GlobalConstants.EVENT_EXIT or self.event == GlobalConstants.EVENT_CANCEL or self.values[0] == '':
            #retVal = FileSearchModes.choice_None
            exit()
        else:
            fileOps = FileOperations()
            folderChosen = self.values[0]
            if fileOps.isThisValidDirectory(folderChosen):
                retVal = fileOps.folderSlash(folderChosen)
            else:
                retVal = FileSearchModes.choice_None
#         if retVal == FileSearchModes.choice_None:
#             sg.popup('Please select a valid folder next time. Exiting now.')
#             exit()    
        return retVal   

class StringInputMenu:
    def __init__(self):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35       
    
    def showUserTheMenu(self, topText, bottomText):
        #---choose mode of running
        layout = []
        for s in topText:
            layout.append([sg.Text(s, justification='left')])
        layout.append([sg.InputText('')])
        for s in bottomText:
            layout.append([sg.Text(s, text_color='grey', justification='left')])        
        layout.append([sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')])
        layout.append([sg.Button(GlobalConstants.EVENT_CANCEL), sg.Button('Ok')])
                
#         layout = [
#                     [sg.Text('Which files do you want to get rid of? (names are case sensitive) ', justification='left')],
#                     [sg.InputText('')],        
#                     [sg.Text('For example, you could type them as comma separated file names: ', text_color='grey', justification='left')],
#                     [sg.Text('Thumbs.db, Desktop.ini', text_color='grey', justification='left')],                    
#                     [sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')],
#                     [sg.Button(GlobalConstants.EVENT_CANCEL), sg.Button('Ok')] #[sg.Cancel(), sg.OK()]
#                   ]
        window = sg.Window('', layout, grab_anywhere=False, element_justification='right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        filesChosen = self.values[0]
        if self.event == sg.WIN_CLOSED or self.event == GlobalConstants.EVENT_EXIT or self.event == GlobalConstants.EVENT_CANCEL or filesChosen == '':
            if filesChosen == '':
                print('Exited. No filename was mentioned')
            exit()
        return filesChosen.split(',')
    
class FileDuplicateSearchBinaryMode:
    def __init__(self, foldername):
        self.CHUNK_SIZE = 8 * 1024        
        self.fileOps = FileOperations()
        self.baseFolder = foldername
        self.folderForDuplicateFiles = self.baseFolder + GlobalConstants.duplicateFilesFolder
        self.fileOps.createDirectoryIfNotExisting(self.folderForDuplicateFiles)
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.report = []
    
    def search(self):        
        atLeastOneDuplicateFound = False
        #---initiate search for duplicates
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            print('Searching in ', path)
            if path == self.folderForDuplicateFiles:#dont search an existing duplicates folder
                continue
            for fileOrdinal in range(len(filenames)):#for each file in the folder
                duplicateOrdinal = 0
                filesize = self.fileSizes[folderOrdinal][fileOrdinal]
                filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                if filename == GlobalConstants.alreadyProcessedFile:
                    continue
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
                        if filenameToCompare == GlobalConstants.alreadyProcessedFile:
                            continue
                        
                        filesizeToCompare = self.fileSizes[folderOrdinalToCompare][fileOrdinalToCompare]
                        if filesize == filesizeToCompare:#initial match found based on size
                                                                                   
                            #---now compare based on file contents
                            filesAreSame = self.__compareEntireFiles__(path + filename, pathToCompare + filenameToCompare)
                            if filesAreSame:
                                atLeastOneDuplicateFound = True
                                duplicateOrdinal = duplicateOrdinal + 1
                                self.__moveFileToSeparateFolder__(folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare, duplicateOrdinal)
                                self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal)
        if not atLeastOneDuplicateFound:
            self.report = ["No duplicates found"]
    
    def showReport(self):
        for aLine in self.report:
            print(aLine)
            
    def saveReport(self):
        #TODO: use fileOps to write the report to a file
        pass
    
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
        self.fileOps.moveFile(dupFolder, dupFile, self.folderForDuplicateFiles, fileName+"_"+str(duplicateOrdinal)+fileExtension) 
        reportString = folder + file + "'s duplicate: " + dupFolder + dupFile + " is renamed and moved to " + self.folderForDuplicateFiles
        self.report.append(reportString)
    
    def __markAlreadyProcessedFile__(self, folderOrdinal, fileOrdinal):
        self.filesInFolder[folderOrdinal][fileOrdinal] = GlobalConstants.alreadyProcessedFile            
    
    def __compareEntireFiles__(self, filename1, filename2):
        with open(filename1, 'rb') as filePointer1, open(filename2, 'rb') as filePointer2:
            while True:
                chunk1 = filePointer1.read(self.CHUNK_SIZE)#TODO: try catch
                chunk2 = filePointer2.read(self.CHUNK_SIZE)
                if chunk1 != chunk2:
                    return False
                if not chunk1:#if chunk is of zero bytes (nothing more to read from file), return True because chunk2 will also be zero. If it wasn't zero, the previous if would've been False
                    return True                        
    

class FileSearchDeleteSpecifiedFiles:
    def __init__(self, foldername):
        self.fileOps = FileOperations()
        self.baseFolder = foldername
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.report = []
    
    def searchAndDestroy(self, filesToDelete):
        atLeastOneFileFound = False
        #---initiate search for duplicates
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            print('Searching in ', path)
            for fileOrdinal in range(len(filenames)):#for each file in the folder
                filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                if filename in filesToDelete:
                    self.__deleteFile__(folderOrdinal, fileOrdinal)
        if not atLeastOneFileFound:
            self.report = ["No files found"]
    
    def showReport(self):
        for aLine in self.report:
            print(aLine)
    
    def __deleteFile__(self, folderOrdinal, fileOrdinal):  
        #TODO: if delete not possible, mention it in the generated report
        folder = self.folderPaths[folderOrdinal]
        file = self.filesInFolder[folderOrdinal][fileOrdinal]        
        self.fileOps.deleteFile(folder, file)
        reportString = "Deleted " + folder + file
        self.report.append(reportString)
                          

# class FileSearchImageMode:
#     def __init__(self, foldername):        
#         self.fileOps = FileOperations()
#         self.baseFolder = foldername
#         self.supportedFileExtensions = ['jpg', 'png', 'gif', 'webp']
#         self.folderForDuplicateImages = self.baseFolder + GlobalConstants.duplicateImagesFolder
#         self.fileOps.createDirectoryIfNotExisting(self.folderForDuplicateImages)
#         self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
#         self.report = []
#     
#     def search(self):
#         atLeastOneDuplicateFound = False
#         #---initiate search for duplicates
#         for folderOrdinal in range(len(self.folderPaths)):#for each folder
#             filenames = self.filesInFolder[folderOrdinal]
#             path = self.folderPaths[folderOrdinal]
#             if path == self.folderForDuplicateImages:#don't search an existing duplicates folder
#                 continue
#             for fileOrdinal in range(len(filenames)):#for each file in the folder
#                 duplicateOrdinal = 0
#                 filename = self.filesInFolder[folderOrdinal][fileOrdinal]
#                 if filename == GlobalConstants.alreadyProcessedFile:
#                     continue
#                 if not self.__isSupportedFileExtension__(folderOrdinal, fileOrdinal):
#                     self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal) 
#                     continue                 
#                 #---compare with all files
#                 for folderOrdinalToCompare in range(len(self.folderPaths)):#for each folder
#                     filenamesToCompare = self.filesInFolder[folderOrdinalToCompare]
#                     pathToCompare = self.folderPaths[folderOrdinalToCompare] 
#                     if pathToCompare == self.folderForDuplicateImages:#don't search an existing duplicates folder
#                         continue                                                                 #---check based on file extension                   
#                     for fileOrdinalToCompare in range(len(filenamesToCompare)):#for each file in the folder
#                         filenameToCompare = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
#                         if folderOrdinal == folderOrdinalToCompare and fileOrdinal == fileOrdinalToCompare:#skip self
#                             continue
#                         if filenameToCompare == GlobalConstants.alreadyProcessedFile:
#                             continue
#                         #---check based on file extension
#                         if not self.__isSupportedFileExtension__(folderOrdinalToCompare, fileOrdinalToCompare):
#                             self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare) 
#                             continue                                                                      
#                         #---now compare based on file contents
#                         filesAreSame = self.__compareEntireImage__(path + filename, pathToCompare + filenameToCompare)
#                         if filesAreSame:
#                             atLeastOneDuplicateFound = True
#                             duplicateOrdinal = duplicateOrdinal + 1
#                             self.__moveFileToSeparateFolder__(folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare, duplicateOrdinal)
#                             self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
#                 self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal)
#         if not atLeastOneDuplicateFound:
#             self.report = ["No duplicates found"]
#     
#     def __isSupportedFileExtension__(self, folderOrdinalToCompare, fileOrdinalToCompare):
#         folder = self.folderPaths[folderOrdinalToCompare]
#         file = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]        
#         filename, fileExtension = os.path.splitext(folder + file)
#         return fileExtension in self.supportedFileExtensions:
#     
#     def showReport(self):
#         for aLine in self.report:
#             print(aLine)
#     
#     def __moveFileToSeparateFolder__(self, folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare, duplicateOrdinal):
#         #TODO: if move not possible, copy and mention any move issues in the generated report
#         folder = self.folderPaths[folderOrdinal]
#         file = self.filesInFolder[folderOrdinal][fileOrdinal]        
#         dupFolder = self.folderPaths[folderOrdinalToCompare]
#         dupFile = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
#         self.fileOps.moveFile(dupFolder, dupFile, self.folderForDuplicateImages, file+"_"+str(duplicateOrdinal)) #TODO: can have a try catch to check if directory exists, before doing the move
#         reportString = folder + file + "'s duplicate: " + dupFolder + dupFile + " is renamed and moved to " + self.folderForDuplicateImages
#         self.report.append(reportString)
#     
#     def __markAlreadyProcessedFile__(self, folderOrdinal, fileOrdinal):
#         self.filesInFolder[folderOrdinal][fileOrdinal] = GlobalConstants.alreadyProcessedFile            
#     
#     def __compareEntireImage__(self, filename1, filename2):
#         imagesSame = True        
#         image1 = Image.open(filename1)
#         image1 = image1.convert('RGB')
#         width1 = image1.size[0]
#         height1 = image1.size[1]
#         if width1 == 0 or height1 == 0:
#             imagesSame = False
#         else:
#             image2 = Image.open(filename2)
#             image2 = image2.convert('RGB')
#             width2 = image2.size[0]
#             height2 = image2.size[1]   
#             if width2 == 0 or height2 == 0 or width1 != width2 or height1 != height2:
#                 imagesSame = False   
#             else:                                            
#                 #---compare pixels
#                 for y in range(0, height1):
#                     for x in range(0, width1):
#                         r1, g1, b1 = image1.getpixel((x, y))
#                         r2, g2, b2 = image2.getpixel((x, y))        
#                         if r1 != r2 or g1 != g2 or b1 != b2:#TODO: this has to be generalized a bit due to conversion losses
#                             imagesSame = False
#                             break
#                     if not imagesSame:
#                         break
#         return imagesSame


#-----------------------------------------------             
#-----------------------------------------------
#------------ PROGRAM STARTS HERE --------------
#-----------------------------------------------
#-----------------------------------------------
if __name__ == '__main__':
    sg.theme('Dark grey 13')  # please make your creations colorful
    #---choosing to do a file or image search
    searchMethod = FirstChoiceMenu()
    searchMethod.showUserTheMenu()
    userChoice = searchMethod.getUserChoice()
    
    #---proceed with file duplicate search menu
    if userChoice == FileSearchModes.choice_fileBinary:
        #---get folder name
        topText = ['Which folder do you want to search in? ']        
        bottomText = ['Duplicate files are assumed to be inside the folder you choose. This', 'program will move all duplicates into a separate, newly created folder']        
        whichFolder = FolderChoiceMenu()
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #---search for diplicates
        fileSearcher = FileDuplicateSearchBinaryMode(folderChosen)
        fileSearcher.search()
        fileSearcher.showReport()
    
#     #---proceed with image search menu
#     """ Image search is useful in cases where for example, an image is in jpg format and the same image is also present in png or webp format and you want to delete one of the duplicates """
#     if userChoice == FileSearchModes.choice_imagePixels:
#         #---get folder name
#         topText = ['Which folder do you want to search in? ']        
#         bottomText = ['Image type does not matter. Search is done using image pixels. This', 'program will move duplicates into a separate, newly created folder']        
#         whichFolder = FolderChoiceMenu()
#         whichFolder.showUserTheMenu(topText, bottomText)
#         folderChosen = whichFolder.getUserChoice()
#         #TODO: User should specify priority of image-type to retain. Eg. If a webp duplicate of jpg is found, should webp be retained or jpg?
    
    #---specify what file to remove from folder and subfolders
    if userChoice == FileSearchModes.choice_residualFiles:
        #---get filenames
        topText = ['Which files do you want to get rid of? (names are case sensitive)']
        bottomText = ['For example, you could type them as comma separated file names: ', 'Thumbs.db, Desktop.ini'] 
        whichFiles = StringInputMenu() #get filename(s)
        whichFiles.showUserTheMenu(topText, bottomText)
        filesToDelete = whichFiles.getUserChoice()
        #---get foldername
        topText = ['Which folder do you want to search in? ']        
        bottomText = ['Subfolders will also be searched to delete these files:', str(filesToDelete)]        
        whichFolder = FolderChoiceMenu() #get folder in which to start recursively searching and deleting files
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #TODO: can have a confirmation dialog box for safety         
        #---search and destroy
        fileDeleter = FileSearchDeleteSpecifiedFiles(folderChosen)
        fileDeleter.searchAndDestroy(filesToDelete)
        fileDeleter.showReport()
    
    print('Program ended')
    
    
    
    