#-----------------------------------------------             
#-----------------------------------------------
#---------------- PARAMETERS -------------------
#-----------------------------------------------
#-----------------------------------------------

class GlobalConstants:
    duplicateFilesFolder = "duplicateFilesFolder/"
    duplicateImagesFolder = "duplicateImagesFolder/"
    previouslySelectedFolderForDuplicatesCheck = "previouslySelectedFolder.txt"
    EVENT_CANCEL = 'Cancel'
    EVENT_EXIT = 'Cancel'
    YES_BUTTON = 'Yes'
    NO_BUTTON = 'No'
    alreadyProcessedFile = "."
    supportedImageFormats = ['jpg', 'jpeg', 'png', 'webp'] #let all extensions mentioned here be in lower case. More can be added after testing.
    FIRST_POSITION_IN_LIST = 0

class FileSearchModes:
    choice_None = 'Exit'
    choice_fileBinary = 'Duplicate file segregation'
    choice_imagePixels = 'Duplicate image segregation'    
    choice_residualFiles = 'Delete files (like Thumbs.db etc.)'
    choice_undoFileMove = 'Undo files that were moved and renamed'
    
class FilenameMatching:
    fullString = "fullString"
    wildcard = "wildcard"
