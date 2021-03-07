'''
File duplicate finder. Special mode for comparing images too.
Created on 19-Feb-2021
@author: Navin
'''

# if running in py3, change the shebang, drop the next import for readability (it does no harm in py3)
from __future__ import print_function   # py2 compatibility
from collections import defaultdict
import PySimpleGUI as sg
import hashlib
import filecmp
import os
import sys

class GlobalConstants:
    duplicatesFolder = "duplicatesFolder/"

class FileOperations:
    def __init__(self):
        self.FULL_FOLDER_PATH = 0
        #self.SUBDIRECTORIES = 1
        self.FILES_IN_FOLDER = 2
    
    """ Get names of files in each folder and subfolder. Also get sizes of files """
    def getNames(self, folderToConsider): 
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
    
    """ Check for folder's existence in current working directory. Create if it does not exist """
    def createDirectoryIfNotExisting(self, folder):
        if not os.path.exists(folder): 
            try: os.makedirs(folder)
            except FileExistsError:#in case there's a race condition where some other process creates the directory before makedirs is called
                pass      
            
    """ Is this a valid directory """
    def isThisValidDirectory(self, folderpath):
        return os.path.exists(folderpath)
            
    """ Adds a slash at the end of the folder name if it isn't already present """
    def folderSlash(self, folderName):
        if folderName.endswith('/') == False: 
            folderName = folderName + '/' 
        return folderName    

class FileSearchModes:
    choice_None = 'Exit'
    choice_fileBinary = 'Duplicate files (byte search)'
    choice_imagePixels = 'Duplicate images (pixel search)'    
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
                    [sg.Combo([FileSearchModes.choice_fileBinary, FileSearchModes.choice_imagePixels, FileSearchModes.choice_residualFiles], default_value=FileSearchModes.choice_fileBinary)],        
                    [sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')],
                    [sg.Cancel(), sg.OK()]
                 ]
        window = sg.Window('', layout, element_justification='right', grab_anywhere=True)    
        self.event, self.values = window.read()        
        window.close()
        
    def getUserChoice(self):
        retVal = None
        if self.event == sg.WIN_CLOSED or self.event == 'Exit' or self.event == sg.Cancel:
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
    
    def showUserTheMenu(self):
        #---choose mode of running
        layout = [
                    [sg.Text('Which folder do you want to search in? ', justification='left')],
                    [sg.Input(), sg.FolderBrowse()],        
                    [sg.Text('Duplicate files are assumed to be inside the folder you choose. This', text_color='grey', justification='left')],
                    [sg.Text('program will move all duplicates into a separate, newly created folder', text_color='grey', justification='left')],                    
                    [sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')],
                    [sg.Cancel(), sg.OK()]
                  ]
        window = sg.Window('', layout, element_justification='right', grab_anywhere=False)    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        retVal = None
        if self.event == sg.WIN_CLOSED or self.event == 'Exit' or self.event == sg.Cancel or self.values[0] == '':
            retVal = FileSearchModes.choice_None
        else:
            fileOps = FileOperations()
            folderChosen = self.values[0]
            if fileOps.isThisValidDirectory(folderChosen):
                retVal = fileOps.folderSlash(folderChosen)
            else:
                retVal = FileSearchModes.choice_None
        if retVal == FileSearchModes.choice_None:
            sg.popup('Please select a valid folder next time. Exiting now.')
            exit()    
        return retVal   

class FileSearchBinaryMode:
    def __init__(self, foldername):
        self.CHUNK_SIZE = 8 * 1024
        self.alreadyProcessedFile = "."
        self.fileOps = FileOperations()
        self.baseFolder = foldername
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getNames(self.baseFolder)
        self.report = []
    
    def search(self):
        #---initiate search for duplicates
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            for fileOrdinal in range(len(filenames)):#for each file in the folder
                filesize = self.fileSizes[folderOrdinal][fileOrdinal]
                filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                if self.fileSizes[folderOrdinal][fileOrdinal] == self.alreadyProcessedFile:
                    continue
                #---compare with all files
                for folderOrdinalToCompare in range(len(self.folderPaths)):#for each folder
                    filenamesToCompare = self.filesInFolder[folderOrdinalToCompare] 
                    for fileOrdinalToCompare in range(len(filenamesToCompare)):#for each file in the folder
                        filenameToCompare = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
                        if folderOrdinal == folderOrdinalToCompare and fileOrdinal == fileOrdinalToCompare:#skip self
                            continue
                        if filenameToCompare == self.alreadyProcessedFile:
                            continue
                        
                        filesizeToCompare = self.fileSizes[folderOrdinalToCompare][fileOrdinalToCompare]
                        if filesize == filesizeToCompare:#initial match found based on size
                            pathToCompare = self.folderPaths[folderOrdinalToCompare]                                                        
                            #---now compare based on file contents
                            filesAreSame = self.__compareEntireFiles__(path + filename, pathToCompare + filenameToCompare)
                            if filesAreSame:
                                self.__moveFileToSeparateFolder__(folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare)
                                self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal)
    
    def __moveFileToSeparateFolder__(self, folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare):
        #Note: Empty files will be identified as duplicates of other empty files. It's normal.  
        #TODO: if move not possible, copy and mention any move issues in the generated report
        folder = self.folderPaths[folderOrdinal]
        file = self.filesInFolder[folderOrdinal][fileOrdinal]        
        dupFolder = self.folderPaths[folderOrdinalToCompare]
        dupFile = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
        #TODO: move file
        reportString = folder + file + "'s duplicate: " + dupFolder + dupFile + " is moved to " + self.baseFolder + GlobalConstants.duplicatesFolder
        self.report.append(reportString)
        print(reportString)
    
    def __markAlreadyProcessedFile__(self, folderOrdinal, fileOrdinal):
        self.filesInFolder[folderOrdinal][fileOrdinal] = self.alreadyProcessedFile            
    
    def __compareEntireFiles__(self, filename1, filename2):
        with open(filename1, 'rb') as filePointer1, open(filename2, 'rb') as filePointer2:
            while True:
                chunk1 = filePointer1.read(self.CHUNK_SIZE)#TODO: try catch
                chunk2 = filePointer2.read(self.CHUNK_SIZE)
                if chunk1 != chunk2:
                    return False
                if not chunk1:#if chunk is of zero bytes (nothing more to read from file), return True because chunk2 will also be zero. If it wasn't zero, the previous if would've been False
                    return True                        
    


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
    
    #---proceed with file search menu
    if userChoice == FileSearchModes.choice_fileBinary:
        whichFolder = FolderChoiceMenu()
        whichFolder.showUserTheMenu()
        folderChosen = whichFolder.getUserChoice()
        fileSearcher = FileSearchBinaryMode(folderChosen)
        fileSearcher.search()
    
    #---proceed with image search menu
    if userChoice == FileSearchModes.choice_imagePixels:
        pass
    
    #---specify what file to remove from folder and subfolders
    if userChoice == FileSearchModes.choice_residualFiles:
        pass
        


# import os, sys
# import Image
# 
# im = Image.open("image.jpg")
# x = 3
# y = 4
# 
# pix = im.load()
# print pix[x,y]
# #------
# photo = Image.open('IN.jpg') #your image
# photo = photo.convert('RGB')
# 
# width = photo.size[0] #define W and H
# height = photo.size[1]
# 
# for y in range(0, height): #each pixel has coordinates
#     row = ""
#     for x in range(0, width):
# 
#         RGB = photo.getpixel((x,y))
#         R,G,B = RGB  #now you can use the RGB value
    
    
    
    