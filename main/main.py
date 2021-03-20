'''
File duplicate finder. Special mode for comparing images too.
Created on 19-Feb-2021
@author: Navin
'''

from __future__ import print_function   # py2 compatibility
import PySimpleGUI as sg
import os
import shutil #for moving file
import datetime
#import filetype
#import Image

#TODO: Use a logger instead of the current print output
#TODO: create a memory of the last location that was searched, and show that as the default when doing a folder search
#TODO: Use a CI to automatically run tests and to use pyinstaller to generate an installer file
#TODO: Add an option to undo the duplicate file move
#TODO: Add a progress bar and also output progress percentage with current time to command prompt.

#-----------------------------------------------             
#-----------------------------------------------
#---------------- PARAMETERS -------------------
#-----------------------------------------------
#-----------------------------------------------

class GlobalConstants:
    duplicateFilesFolder = "duplicateFilesFolder/"
    duplicateImagesFolder = "duplicateImagesFolder/"
    EVENT_CANCEL = 'Cancel'
    EVENT_EXIT = 'Cancel'
    YES_BUTTON = 'Yes'
    NO_BUTTON = 'No'
    alreadyProcessedFile = "."

class FileSearchModes:
    choice_None = 'Exit'
    choice_fileBinary = 'Duplicate file segregation'
    choice_imagePixels = 'Duplicate image segregation'    
    choice_residualFiles = 'Delete files (like Thumbs.db etc.)'
    

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
        
    def writeLinesToFile(self, pathAndFilename, report):
        fileHandle = open(pathAndFilename, 'w')
        for line in report:
            fileHandle.write(line)
            fileHandle.write("\n")
        fileHandle.close()
    
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
    
    def compareEntireFiles(self, filename1, filename2):
        with open(filename1, 'rb') as filePointer1, open(filename2, 'rb') as filePointer2:
            while True:
                chunk1 = filePointer1.read(self.CHUNK_SIZE_FOR_BINARY_FILE_COMPARISON)#TODO: try catch
                chunk2 = filePointer2.read(self.CHUNK_SIZE_FOR_BINARY_FILE_COMPARISON)
                if chunk1 != chunk2:
                    return False
                if not chunk1:#if chunk is of zero bytes (nothing more to read from file), return True because chunk2 will also be zero. If it wasn't zero, the previous if would've been False
                    return True       

    
#-----------------------------------------------             
#-----------------------------------------------
#-------------------- MENUS --------------------
#-----------------------------------------------
#-----------------------------------------------    
class DropdownChoicesMenu:
    def __init__(self):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35 #length of a line that separates the dropdown from the Ok and Cancel buttons. The line was introduced to not have the widgets too close to each other (aesthetics). 
        
    def setHorizontalSeparationLengthTo(self, value):
        #If this value ever needs to be set, it has to be called immediately after the class is initialized, and before calling any other function of this class
        self.horizontalSepLen = value   
    
    def showUserTheMenu(self, displayText, dropdownOptions, defaultDropDownOption):
        #---choose mode of running    
        layout = []
        for text in displayText:
            layout.append([sg.Text(text)])
        layout.append([sg.Combo(dropdownOptions, default_value=defaultDropDownOption)])
        layout.append([sg.Text('_' * self.horizontalSepLen, justification='right', text_color='black')])
        layout.append([sg.Button(GlobalConstants.EVENT_CANCEL), sg.OK()])
        
        window = sg.Window('', layout, element_justification='right', grab_anywhere=True) #The justification was kept "right" because the user clicks the arrow of the dropdown on the right side and since the OK/Cancel buttons were usually on the left side, it was a pain to have to drag the mouse pointer all the way to the left. Since I couldn't find a way to justify the buttons to the right, I had to justify all elements to the right. The better way is to justify the text to the left and justify the buttons to the right. If PySimpleGUI is improved to support this, a better justification can be implemented in this program.  
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
    
class YesNoMenu:
    def __init__(self):
        self.event = None
        self.values = None
    
    def showUserTheDialogBox(self, questionText):
        layout = []
        for s in questionText:
            layout.append([sg.Text(s, justification='right')])

        layout.append([sg.Button(GlobalConstants.YES_BUTTON), sg.Button(GlobalConstants.NO_BUTTON)])

        window = sg.Window('', layout, grab_anywhere=False, element_justification='right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        answer = False #by default, the answer is "No"
        if self.event == sg.WIN_CLOSED or self.event == GlobalConstants.EVENT_EXIT:
            print('Exited. No choice made')
            exit()
        if self.event == GlobalConstants.YES_BUTTON:
            print('CASE SENSITIVE')
            answer = True
        return answer
        
        
#-----------------------------------------------             
#-----------------------------------------------
#-------------------- REPORTS ------------------
#-----------------------------------------------
#-----------------------------------------------        
class Reports:
    def __init__(self, folderToStoreReport):
        self.fileOps = FileOperations()
        self.folderToStoreReport = folderToStoreReport
        self.report = []
        self.reportPathAndFile = 'None'
    
    def add(self, text):
        self.report.append(text)
        
    def generateReport(self, shouldWriteReportToFile = False):
        for aLine in self.report:
            print(aLine)
        if shouldWriteReportToFile:
            self.reportPathAndFile = self.folderToStoreReport + "Report_" + str(datetime.datetime.now()) + ".txt"
            self.fileOps.writeLinesToFile(self.reportPathAndFile, self.report)
        sg.popup('Completed. Report: ' + self.reportPathAndFile, keep_on_top=True)            
        
#     def getReportPathAndFilename(self):                
#         return self.reportPathAndFile
    
    
#-----------------------------------------------             
#-----------------------------------------------
#------------- PRIMARY OPERATIONS --------------
#-----------------------------------------------
#-----------------------------------------------    
class FileDuplicateSearchBinaryMode:
    def __init__(self, foldername):
        self.fileOps = FileOperations()
        self.baseFolder = foldername
        self.folderForDuplicateFiles = self.baseFolder + GlobalConstants.duplicateFilesFolder        
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.reports = Reports(self.folderForDuplicateFiles)
        self.reports.add('Searching in : ' + self.baseFolder)
        self.reports.add('Duplicates will be stored in: ' + self.folderForDuplicateFiles)
        self.atLeastOneDuplicateFound = False
    
    def search(self):        
        firstDuplicate = False        
        #---initiate search for duplicates
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            if path == self.folderForDuplicateFiles:#dont search an existing duplicates folder
                continue
            print('Searching in ', path)                
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
                    print('Comparing in ', pathToCompare)                        
                    for fileOrdinalToCompare in range(len(filenamesToCompare)):#for each file in the folder
                        filenameToCompare = self.filesInFolder[folderOrdinalToCompare][fileOrdinalToCompare]
                        if folderOrdinal == folderOrdinalToCompare and fileOrdinal == fileOrdinalToCompare:#skip self
                            continue
                        if filenameToCompare == GlobalConstants.alreadyProcessedFile:
                            continue
                        #print('Is ', filename, '==', filenameToCompare)                        
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
        if not self.atLeastOneDuplicateFound:
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
        self.fileOps.moveFile(dupFolder, dupFile, self.folderForDuplicateFiles, fileName+"_"+str(duplicateOrdinal)+fileExtension) 
        reportString = folder + file + "'s duplicate: " + dupFolder + dupFile + " is renamed and moved to " + self.folderForDuplicateFiles
        self.reports.add(reportString)
    
    def __markAlreadyProcessedFile__(self, folderOrdinal, fileOrdinal):
        self.filesInFolder[folderOrdinal][fileOrdinal] = GlobalConstants.alreadyProcessedFile            
     
#     def getReportPathAndFilename(self):
#         return self.reports.getReportPathAndFilename()    

class FileSearchDeleteSpecifiedFiles:
    def __init__(self, foldername):
        self.fileOps = FileOperations()
        self.baseFolder = foldername
        self.folderPaths, self.filesInFolder, self.fileSizes = self.fileOps.getFileNamesOfFilesInAllFoldersAndSubfolders(self.baseFolder)
        self.reports = Reports(self.baseFolder)
        self.reports.add('Searching in: '+self.baseFolder)
        self.atLeastOneFileFound = False
    
    def searchAndDestroy(self, filesToDelete, caseSensitive):
        self.reports.add("Files to delete: " + str(filesToDelete))
        
        #---initiate search for duplicates
        for folderOrdinal in range(len(self.folderPaths)):#for each folder
            filenames = self.filesInFolder[folderOrdinal]
            path = self.folderPaths[folderOrdinal]
            print('Searching in ', path)
            for fileOrdinal in range(len(filenames)):#for each file in the folder
                filename = self.filesInFolder[folderOrdinal][fileOrdinal]
                print('Filename:', filename, ", ToDel:", filesToDelete)
                if not caseSensitive:
                    filename = filename.lower()
                if filename in filesToDelete:
                    self.__deleteFile__(folderOrdinal, fileOrdinal)
        if not self.atLeastOneFileFound:
            self.reports.add("No files found")
        self.reports.generateReport(self.atLeastOneFileFound)
    
    def __deleteFile__(self, folderOrdinal, fileOrdinal):  
        #TODO: if delete not possible, mention it in the generated report and don't make atLeastOneFileFound true
        folder = self.folderPaths[folderOrdinal]
        file = self.filesInFolder[folderOrdinal][fileOrdinal]        
        self.fileOps.deleteFile(folder, file)
        reportString = "Deleted " + folder + file
        self.reports.add(reportString)
        self.atLeastOneFileFound = True
        
#     def getReportPathAndFilename(self):
#         return self.reports.getReportPathAndFilename()
    

#-----------------------------------------------             
#-----------------------------------------------
#------------ PROGRAM STARTS HERE --------------
#-----------------------------------------------
#-----------------------------------------------
if __name__ == '__main__':
    sg.theme('Dark grey 13')  # please make your creations colorful
    #---choosing to do a file or image search
    searchMethod = DropdownChoicesMenu()
    displayText = ['What kind of operation do you want to do?']
    dropdownOptions = [FileSearchModes.choice_fileBinary, FileSearchModes.choice_imagePixels, FileSearchModes.choice_residualFiles]
    defaultDropDownOption = FileSearchModes.choice_fileBinary
    searchMethod.showUserTheMenu(displayText, dropdownOptions, defaultDropDownOption)
    userChoice = searchMethod.getUserChoice()
    
    #---proceed with file duplicate search menu
    if userChoice == FileSearchModes.choice_fileBinary:
        #---get folder name
        topText = ['Which folder do you want to search in? ']        
        bottomText = ['Duplicate files are assumed to be inside the folder you choose. This', 'program will move all duplicates into a separate, newly created folder']        
        whichFolder = FolderChoiceMenu()
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #---search for duplicates
        fileSearcher = FileDuplicateSearchBinaryMode(folderChosen)
        fileSearcher.search()
    
    #---proceed with image search menu
    """ Image search is useful in cases where for example, an image is in jpg format and the same image is also present in png format and you want to delete one of the duplicates. It can also detect images that are approximately similar """
    if userChoice == FileSearchModes.choice_imagePixels:
        #---get folder name
        topText = ['Which folder do you want to search in? ']        
        bottomText = ['Image type does not matter. Search is done using image pixels. This', 'program will move duplicates into a separate, newly created folder']        
        whichFolder = FolderChoiceMenu()
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #TODO: User should specify priority of image-type to retain. Eg. If a webp duplicate of jpg is found, should webp be retained or jpg?
    
    #---specify what file to remove from folder and subfolders
    if userChoice == FileSearchModes.choice_residualFiles:
        #---get filenames
        topText = ['Which files do you want to get rid of?', '(menus for case sensitivity and folder will be presented soon)']
        bottomText = ['For example, you could type them as comma separated file names: ', 'Thumbs.db, Desktop.ini'] 
        whichFiles = StringInputMenu() #get filename(s)
        whichFiles.showUserTheMenu(topText, bottomText)
        filesToDelete = whichFiles.getUserChoice()
        #---get case sensitivity choice
        yesNoMenuText = ['Should filenames be case sensitive?']
        yesNo = YesNoMenu()
        yesNo.showUserTheDialogBox(yesNoMenuText)
        caseSensitive = yesNo.getUserChoice()
        if not caseSensitive:
            filesToDelete = [x.lower() for x in filesToDelete]
        #---get foldername
        topText = ['Which folder do you want to search in? ']        
        bottomText = ['Subfolders will also be searched to delete these files:', str(filesToDelete)]        
        whichFolder = FolderChoiceMenu() #get folder in which to start recursively searching and deleting files
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #TODO: can have a confirmation dialog box for safety         
        #---search and destroy
        fileDeleter = FileSearchDeleteSpecifiedFiles(folderChosen)
        fileDeleter.searchAndDestroy(filesToDelete, caseSensitive)        
    
    print('Program ended')
    
    
    
    
