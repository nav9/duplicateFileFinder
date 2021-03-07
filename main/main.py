'''
File duplicate finder. Special mode for comparing images too.
Created on 19-Feb-2021
@author: navin
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
                #---compare with all files
                for folderOrdinalToCompare in range(len(self.folderPaths)):#for each folder 
                    for fileOrdinalToCompare in range(len(filenames)):#for each file in the folder
                        if folderOrdinal == folderOrdinalToCompare and fileOrdinal == fileOrdinalToCompare:#skip self
                            continue
                        if self.fileSizes[folderOrdinalToCompare][fileOrdinalToCompare] == self.alreadyProcessedFile:
                            continue
                        filesizeToCompare = self.fileSizes[folderOrdinalToCompare][fileOrdinalToCompare]
                        if filesize == filesizeToCompare:#initial match found based on size
                            pathToCompare = self.folderPaths[folderOrdinalToCompare]
                            filenameToCompare = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
                            #---now compare based on file contents
                            filesAreSame = self.__compareEntireFiles__(path + filename, pathToCompare + filenameToCompare)
                            if filesAreSame:
                                self.__moveFileToSeparateFolder__(folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare)
                                self.__markAlreadyProcessedFile__(folderOrdinalToCompare, fileOrdinalToCompare)
                self.__markAlreadyProcessedFile__(folderOrdinal, fileOrdinal)
    
    def __moveFileToSeparateFolder__(self, folderOrdinal, fileOrdinal, folderOrdinalToCompare, fileOrdinalToCompare):
        #if move not possible, copy and mention move issue in report
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
        

    
    #---get folder name
#     layout = [  [sg.Text('Filename')],
#                 [sg.Input(), sg.FileBrowse()], 
#                 [sg.OK(), sg.Cancel()]] 
#     
#     window = sg.Window('Get filename example', layout)
#     
#     event, values = window.read()
#     window.close()





# def chunk_reader(fobj, chunk_size=1024):
#     """Generator that reads a file in chunks of bytes"""
#     while True:
#         chunk = fobj.read(chunk_size)
#         if not chunk:
#             return
#         yield chunk
# 
# 
# def get_hash(filename, first_chunk_only=False, hash=hashlib.sha1):
#     hashobj = hash()
#     file_object = open(filename, 'rb')
# 
#     if first_chunk_only:
#         hashobj.update(file_object.read(1024))
#     else:
#         for chunk in chunk_reader(file_object):
#             hashobj.update(chunk)
#     hashed = hashobj.digest()
# 
#     file_object.close()
#     return hashed
# 
# 
# def check_for_duplicates(paths, hash=hashlib.sha1):
#     hashes_by_size = defaultdict(list)  # dict of size_in_bytes: [full_path_to_file1, full_path_to_file2, ]
#     hashes_on_1k = defaultdict(list)  # dict of (hash1k, size_in_bytes): [full_path_to_file1, full_path_to_file2, ]
#     hashes_full = {}   # dict of full_file_hash: full_path_to_file_string
# 
#     for path in paths:
#         for dirpath, dirnames, filenames in os.walk(path):
#             # get all files that have the same size - they are the collision candidates
#             for filename in filenames:
#                 full_path = os.path.join(dirpath, filename)
#                 try:
#                     # if the target is a symlink (soft one), this will 
#                     # dereference it - change the value to the actual target file
#                     full_path = os.path.realpath(full_path)
#                     file_size = os.path.getsize(full_path)
#                     hashes_by_size[file_size].append(full_path)
#                 except (OSError,):
#                     # not accessible (permissions, etc) - pass on
#                     continue
# 
# 
#     # For all files with the same file size, get their hash on the 1st 1024 bytes only
#     for size_in_bytes, files in hashes_by_size.items():
#         if len(files) < 2:
#             continue    # this file size is unique, no need to spend CPU cycles on it
# 
#         for filename in files:
#             try:
#                 small_hash = get_hash(filename, first_chunk_only=True)
#                 # the key is the hash on the first 1024 bytes plus the size - to
#                 # avoid collisions on equal hashes in the first part of the file
#                 # credits to @Futal for the optimization
#                 hashes_on_1k[(small_hash, size_in_bytes)].append(filename)
#             except (OSError,):
#                 # the file access might've changed till the exec point got here 
#                 continue
# 
#     # For all files with the hash on the 1st 1024 bytes, get their hash on the full file - collisions will be duplicates
#     for __, files_list in hashes_on_1k.items():
#         if len(files_list) < 2:
#             continue    # this hash of fist 1k file bytes is unique, no need to spend cpy cycles on it
# 
#         for filename in files_list:
#             try: 
#                 full_hash = get_hash(filename, first_chunk_only=False)
#                 duplicate = hashes_full.get(full_hash)
#                 if duplicate:
#                     print("Duplicate found: {} and {}".format(filename, duplicate))
#                 else:
#                     hashes_full[full_hash] = filename
#             except (OSError,):
#                 # the file access might've changed till the exec point got here 
#                 continue




#     if sys.argv[1:]:
#         check_for_duplicates(sys.argv[1:])
#     else:
#         print("Please pass the paths to check as parameters to the script")


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
    
    
    
    