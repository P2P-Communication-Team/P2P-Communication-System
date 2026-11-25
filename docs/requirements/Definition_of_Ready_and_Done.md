# Definition of Ready (DoR) and Definition of Done (DoD)

**Project:** P2P Communication System  
**Purpose:** To make sure that work items are clearly understood before development starts and are properly completed before they are accepted.

---

## 1. Definition of Ready (DoR)

A task, user story, or issue is considered **Ready** when it contains enough information for a team member to start working on it without major confusion.

A task must meet the following conditions:

- [ ] The task has a clear title and description.
- [ ] The purpose and expected outcome of the task are explained.
- [ ] Requirements and scope are clearly defined.
- [ ] Acceptance criteria are written and can be checked.
- [ ] Required inputs, outputs, and expected system behavior are identified.
- [ ] Dependencies on other tasks or components are identified.
- [ ] Required designs, diagrams, or reference documents are available, if applicable.
- [ ] The task is small and clear enough to be completed within the planned development period.
- [ ] The team has discussed the task and resolved important questions.
- [ ] The task has been prioritized and assigned, or is ready to be assigned.

**Rule:** Development should not begin until the important DoR conditions are satisfied. If information is missing, the task should be clarified first.

## 2. Definition of Done (DoD)

A task is considered **Done** when its implementation is complete, verified, reviewed, and meets the agreed acceptance criteria.

A task must meet the following conditions:

- [ ] The implementation matches the approved requirements.
- [ ] All acceptance criteria are satisfied.
- [ ] The code follows the project's agreed coding standards.
- [ ] The implementation has been tested.
- [ ] Relevant unit, integration, or system tests pass, where applicable.
- [ ] Errors and major issues found during testing have been fixed.
- [ ] The change does not break existing system functionality.
- [ ] Code has been reviewed and necessary review comments have been addressed.
- [ ] Relevant documentation, including setup instructions or the SRS where needed, has been updated.
- [ ] The changes have been committed and pushed to the appropriate Git branch.
- [ ] The work has been integrated into the target branch according to the team's workflow.
- [ ] The task has been demonstrated or verified against its acceptance criteria.
- [ ] The issue or task status has been updated and the work is ready for acceptance.

**Rule:** A task must not be marked Done just because coding is complete. It is Done only after the required testing, review, documentation, and acceptance checks are complete.

## 3. Difference Between DoR and DoD

| Definition of Ready (DoR) | Definition of Done (DoD) |
|---|---|
| Applied before work starts. | Applied before work is accepted as complete. |
| Confirms that the task is clear and actionable. | Confirms that the task is implemented and verified. |
| Focuses on requirements, scope, and dependencies. | Focuses on implementation, testing, review, and documentation. |
| Prevents starting unclear tasks. | Prevents incomplete work from being marked complete. |

## 4. Applying DoR and DoD to the P2P Communication System

For this project, DoR should be checked when a new issue or development task is added to the project board. The team should confirm that the expected behavior, requirements, and acceptance criteria are clear before assigning the work.

DoD should be checked when a task is completed. For example, a change to a communication feature should be tested to confirm that it works as expected and does not negatively affect existing functionality. Relevant code, documentation, and Git changes should also be reviewed before the issue is closed.

## 5. Review and Updates

The team should review these definitions when the project workflow or development requirements change. Any changes should be agreed upon by the team and applied consistently to project tasks.
